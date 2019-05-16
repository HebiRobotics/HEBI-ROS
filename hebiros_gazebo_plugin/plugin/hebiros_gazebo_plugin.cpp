#include "hebiros_gazebo_plugin.h"
#include "sensor_msgs/Imu.h"

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
    physics::JointWrench wrench = joint->GetForceTorque(0);
    return (-1 * (trans * wrench.body1Torque)).Z();
  }
};

using GazeboWrapper = GazeboHelper<GetGazeboVersion(GAZEBO_VERSION), physics::JointPtr>;

//Load the model and sdf from Gazebo
void HebirosGazeboPlugin::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf) {

  HebiGazeboPlugin::Load(_model, _sdf);

  int argc = 0;
  char **argv = NULL;
  ros::init(argc, argv, "hebiros_gazebo_plugin_node");

  this->robot_namespace = "";
  if (_sdf->HasElement("robotNamespace")) {
    this->robot_namespace = _sdf->GetElement("robotNamespace")->Get<std::string>();
  }
  if (this->robot_namespace == "") {
    this->n.reset(new ros::NodeHandle);
  } else {
    this->n.reset(new ros::NodeHandle(this->robot_namespace));
  }

  this->update_connection = event::Events::ConnectWorldUpdateBegin (
    boost::bind(&HebirosGazeboPlugin::OnUpdate, this, _1));

  ROS_INFO("Loaded hebiros gazebo plugin");
}

//Update the joints at every simulation iteration
void HebirosGazeboPlugin::OnUpdate(const common::UpdateInfo & _info) {

  if (this->first_sim_iteration) {
    this->first_sim_iteration = false;
    this->add_group_srv =
      this->n->advertiseService<AddGroupFromNamesSrv::Request, AddGroupFromNamesSrv::Response>(
      "/hebiros_gazebo_plugin/add_group", boost::bind(
      &HebirosGazeboPlugin::SrvAddGroup, this, _1, _2));
  }

  ros::Time current_time = ros::Time::now();

  for (auto group_pair : hebiros_groups) {
    auto hebiros_group = group_pair.second;

    // TODO: change this to update each module...?
    // Get the time elapsed since the last iteration
    ros::Duration iteration_time = current_time - hebiros_group->prev_time;
    hebiros_group->prev_time = current_time;
    if (hebiros_group->group_added) {
      UpdateGroup(hebiros_group, iteration_time);
    }
  }
}

//Publish feedback and compute PID control to command a joint
void HebirosGazeboPlugin::UpdateGroup(std::shared_ptr<HebirosGazeboGroup> hebiros_group, const ros::Duration& iteration_time) {
  for (auto joint_pair : hebiros_group->joints) {

    auto hebiros_joint = joint_pair.second;

    physics::JointPtr joint = model_->GetJoint(hebiros_joint->name+"/"+hebiros_joint->model_name);

    if (joint) {

      ros::Time current_time = ros::Time::now();
      ros::Duration elapsed_time = current_time - hebiros_group->start_time;
      ros::Duration feedback_time = current_time - hebiros_group->prev_feedback_time;

      int i = hebiros_joint->feedback_index;

      joint->SetProvideFeedback(true);
      double velocity = joint->GetVelocity(0);

      double position = GazeboWrapper::position(joint);
      double effort = GazeboWrapper::effort(joint);

      hebiros_group->feedback.position[i] = position;
      hebiros_group->feedback.velocity[i] = velocity;
      hebiros_group->feedback.effort[i] = effort;

      const auto& accel = hebiros_joint->getAccelerometer();
      hebiros_group->feedback.accelerometer[i].x = accel.x();
      hebiros_group->feedback.accelerometer[i].y = accel.y();
      hebiros_group->feedback.accelerometer[i].z = accel.z();
      const auto& gyro = hebiros_joint->getGyro();
      hebiros_group->feedback.gyro[i].x = gyro.x();
      hebiros_group->feedback.gyro[i].y = gyro.y();
      hebiros_group->feedback.gyro[i].z = gyro.z();

      // Add temperature feedback
      hebiros_group->feedback.motor_winding_temperature[i] = hebiros_joint->temperature.getMotorWindingTemperature();
      hebiros_group->feedback.motor_housing_temperature[i] = hebiros_joint->temperature.getMotorHousingTemperature();
      hebiros_group->feedback.board_temperature[i] = hebiros_joint->temperature.getActuatorBodyTemperature();

      if (hebiros_group->command_received) {
        // TODO: SENDER ID!!!!! Generate this properly.
        uint64_t sender_id = 1;
        int j = hebiros_joint->command_index;
        auto p_cmd = std::numeric_limits<double>::quiet_NaN();
        auto v_cmd = std::numeric_limits<double>::quiet_NaN();
        auto e_cmd = std::numeric_limits<double>::quiet_NaN();
        if (j < hebiros_group->command_target.position.size())
          p_cmd = hebiros_group->command_target.position[j];
        if (j < hebiros_group->command_target.velocity.size())
          v_cmd = hebiros_group->command_target.velocity[j];
        if (j < hebiros_group->command_target.effort.size())
          e_cmd = hebiros_group->command_target.effort[j];

        hebiros_joint->setCommand(p_cmd, v_cmd, e_cmd, sender_id, hebiros_group->command_lifetime/1000.0, elapsed_time.toSec());


        // TODO: move this to the joint's update function...and have the right default for 0 pwm!
        double force = HebirosGazeboController::ComputeForce(hebiros_joint,
          position, velocity, effort, iteration_time);

        joint->SetForce(0, force);

        hebiros_group->feedback.position_command[j] = p_cmd;
        hebiros_group->feedback.velocity_command[j] = v_cmd;
        hebiros_group->feedback.effort_command[j] = e_cmd;
      }

      if (!hebiros_group->feedback_pub.getTopic().empty() &&
        feedback_time.toSec() >= 1.0/hebiros_group->feedback_frequency) {

        hebiros_group->feedback_pub.publish(hebiros_group->feedback);
        hebiros_group->prev_feedback_time = current_time;
      }
    }
    else {
      ROS_WARN("Joint %s not found", hebiros_joint->name.c_str());
    }
  }
}

//Service callback which adds a group with corresponding joints
bool HebirosGazeboPlugin::SrvAddGroup(AddGroupFromNamesSrv::Request &req,
  AddGroupFromNamesSrv::Response &res) {

  if (hebiros_groups.find(req.group_name) != hebiros_groups.end()) {
    ROS_WARN("Group %s already exists", req.group_name.c_str());
    return true;
  }

  std::shared_ptr<HebirosGazeboGroup> hebiros_group =
    std::make_shared<HebirosGazeboGroup>(req.group_name, this->n);

  hebiros_groups[req.group_name] = hebiros_group;

  for (int i = 0; i < req.families.size(); i++) {
    for (int j = 0; j < req.names.size(); j++) {

      if ((req.families.size() == 1) ||
        (req.families.size() == req.names.size() && i == j)) {

        std::string joint_name = req.families[i]+"/"+req.names[j];
        hebiros_group->feedback.name.push_back(joint_name);

        AddJointToGroup(hebiros_group, joint_name);
      }
    }
  }

  int size = hebiros_group->joints.size();

  hebiros_group->feedback.position.resize(size);
  hebiros_group->feedback.motor_winding_temperature.resize(size);
  hebiros_group->feedback.motor_housing_temperature.resize(size);
  hebiros_group->feedback.board_temperature.resize(size);
  hebiros_group->feedback.velocity.resize(size);
  hebiros_group->feedback.effort.resize(size);
  // Default, return "nan" for feedback, until we set something!
  hebiros_group->feedback.position_command.resize(size, std::numeric_limits<float>::quiet_NaN());
  hebiros_group->feedback.velocity_command.resize(size, std::numeric_limits<float>::quiet_NaN());
  hebiros_group->feedback.effort_command.resize(size, std::numeric_limits<float>::quiet_NaN());
  hebiros_group->feedback.accelerometer.resize(size);
  hebiros_group->feedback.gyro.resize(size);

  hebiros_group->feedback_pub = this->n->advertise<FeedbackMsg>(
    "hebiros_gazebo_plugin/feedback/"+req.group_name, 100);

  hebiros_group->group_added = true;

  return true;
}

void updateImu(const boost::shared_ptr<sensor_msgs::Imu const> data) {
}

//Add a joint to an associated group
void HebirosGazeboPlugin::AddJointToGroup(std::shared_ptr<HebirosGazeboGroup> hebiros_group,
  std::string joint_name) {

  std::string model_name = "";
  bool is_x8 = false;
  if (model_->GetJoint(joint_name+"/X5_1")) {
    model_name = "X5_1";
  }
  else if (model_->GetJoint(joint_name+"/X5_4")) {
    model_name = "X5_4";
  }
  else if (model_->GetJoint(joint_name+"/X5_9")) {
    model_name = "X5_9";
  }
  else if (model_->GetJoint(joint_name+"/X8_3")) {
    model_name = "X8_3";
    is_x8 = true;
  }
  else if (model_->GetJoint(joint_name+"/X8_9")) {
    model_name = "X8_9";
    is_x8 = true;
  }
  else if (model_->GetJoint(joint_name+"/X8_16")) {
    model_name = "X8_16";
    is_x8 = true;
  }

  // Get a weak reference to store in the individual groups
  auto raw_joint = addJoint(std::make_unique<hebi::sim::Joint>(joint_name, model_name, is_x8));

  // Temporarily, we store joint subscriptions in the gazebo ros plugin here, since
  // the IMU that generates this data is a separate ROS plugin communicating via ROS
  // messages.
  //
  // This will be abstracted into the ROS plugin wrapper in a subsequent refactor
  hebiros_joint_imu_subs.push_back(n->subscribe<sensor_msgs::Imu>(
    "hebiros_gazebo_plugin/imu/" + joint_name, 100, 
    [raw_joint](const boost::shared_ptr<sensor_msgs::Imu const> data) {
      auto a = data->linear_acceleration;
      auto g = data->angular_velocity;
      raw_joint->updateImu(
          {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z)},
          {static_cast<float>(g.x), static_cast<float>(g.y), static_cast<float>(g.z)});
    }));


  raw_joint->feedback_index = hebiros_group->joints.size();
  raw_joint->command_index = raw_joint->feedback_index;

  HebirosGazeboController::SetSettings(hebiros_group, raw_joint);
  hebiros_group->joints[joint_name] = raw_joint;

}

//Tell Gazebo about this plugin
GZ_REGISTER_MODEL_PLUGIN(HebirosGazeboPlugin);
