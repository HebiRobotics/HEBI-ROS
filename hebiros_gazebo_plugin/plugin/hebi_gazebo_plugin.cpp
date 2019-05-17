#include "hebi_gazebo_plugin.h"
#include <gazebo/physics/physics.hh>

// Checks the content of a string before the first "." at compile time.
// Necessary because Gazebo only defines string version numbers.
// Note: This requires -std=c++14 or higher to compile
constexpr int GetGazeboVersion (char const* string_ver) {
  int res = 0;
  int i = 0;
  while (string_ver[i] != '\0' && string_ver[i] >= '0' && string_ver[i] <= '9') {
    res *= 10;
    res += static_cast<int>(string_ver[i] - '0');
    ++i;
  }
  return res;
}

// This is a templated struct that allows for wrapping some of the Gazebo
// code for which compilation differse between versions; we use partial
// template specialization to compile the appropriate version.
template<int GazeboVersion, typename JointType> struct GazeboHelper {
  static_assert(GazeboVersion == 7 || GazeboVersion == 9, "Unknown version of gazebo");
  // Default implementations so that the above assertion is the only compilation error
  static double position(JointType joint) { return 0; }
  static double effort(JointType joint) { return 0; }
};

using GazeboWrapper = GazeboHelper<GetGazeboVersion(GAZEBO_VERSION), gazebo::physics::JointPtr>;

template<typename JointType> struct GazeboHelper<7, JointType> {
  static double position(JointType joint) {
    return joint->GetAngle(0).Radian(); 
  }

  static double effort(JointType joint) {
    auto trans = joint->GetChild()->GetInitialRelativePose().rot;
    auto wrench = joint->GetForceTorque(0);
    return (-1 * (trans * wrench.body1Torque)).z;
  }
};

template<typename JointType> struct GazeboHelper<9, JointType> {
  static double position(JointType joint) {
    return joint->Position(0);
  }

  static double effort(JointType joint) {
    auto trans = joint->GetChild()->InitialRelativePose().Rot();
    gazebo::physics::JointWrench wrench = joint->GetForceTorque(0);
    return (-1 * (trans * wrench.body1Torque)).Z();
  }
};

namespace hebi {
namespace sim {

void HebiGazeboPlugin::Load(gazebo::physics::ModelPtr model, sdf::ElementPtr sdf) {
  model_ = model;
}

void HebiGazeboPlugin::OnUpdateBase(const gazebo::common::UpdateInfo& info) {

  auto sim_time = info.simTime;

  // Don't allow dt to be zero...
  if (first_time_) {
    prev_time_ = info.simTime;
    first_time_ = false;
    return;
  }
  auto iteration_time = sim_time - prev_time_;
  prev_time_ = sim_time;

  for (auto& joint : joints_) {
    // TODO: do gazebo logic here?
    // TODO: this is a crappy way to get this joint...it doesn't work if the joint changes!
    // Maybe keep these in a parallel list to "joints_" after we refactor those to not be
    // generated by the ros "addGroup" call.
    gazebo::physics::JointPtr gazebo_joint = model_->GetJoint(joint->name+"/"+joint->getModelName());
    if (!gazebo_joint)
    {
      // TODO: output a warning?
      //  WARN("Joint %s not found", hebiros_joint->name.c_str());
      continue;
    }

    gazebo_joint->SetProvideFeedback(true);
    joint->velocity_fbk = gazebo_joint->GetVelocity(0);
    joint->position_fbk = GazeboWrapper::position(gazebo_joint);
    joint->effort_fbk = GazeboWrapper::effort(gazebo_joint);

    joint->update(sim_time.Double());
     
    joint->computePwm(iteration_time.Double());

    double force = joint->generateForce(iteration_time.Double());

    gazebo_joint->SetForce(0, force);
  }
}

Joint* HebiGazeboPlugin::addJoint(std::unique_ptr<Joint> joint) {
  // TODO: keep this from creating duplicate joints for identical model objects...
  // but then we need to refactor groups/joints a bit so the feedback index isn't kept
  // in the group itself.
  auto raw_ptr = joint.get();
  joints_.push_back(std::move(joint));
  return raw_ptr;
}

} // namespace sim
} // namespace hebi
