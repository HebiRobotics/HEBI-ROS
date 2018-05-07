#include "hebiros_services.h"

#include "hebiros.h"

std::map<std::string, ros::ServiceServer> HebirosServices::services;

bool HebirosServices::entryList(
  EntryListSrv::Request &req, EntryListSrv::Response &res) {

  return true;
}

bool HebirosServices::addGroup(
  AddGroupFromNamesSrv::Request &req, AddGroupFromNamesSrv::Response &res,
  std::map<std::string, std::string> joint_full_names) {

  if (req.families.size() != 1 && req.families.size() != req.names.size()) {
    ROS_WARN("Invalid number of familes for group [%s]", req.group_name.c_str());
    return false;
  }

  std::shared_ptr<HebirosGroup> group = HebirosGroup::getGroup(req.group_name);

  ROS_INFO("Created group [%s]:", req.group_name.c_str());
  for (int i = 0; i < req.families.size(); i++) {
    for (int j = 0; j < req.names.size(); j++) {

      if ((req.families.size() == 1) ||
        (req.families.size() == req.names.size() && i == j)) {

        std::string joint_name = req.families[i]+"/"+req.names[j];
        ROS_INFO("/%s/%s", req.group_name.c_str(), joint_name.c_str());

        group->joints[joint_name] = j;

        if (HebirosGroupGazebo::findGroup(req.group_name)) {
          std::shared_ptr<HebirosGroupGazebo> group_gazebo =
            HebirosGroupGazebo::getGroup(req.group_name);
          group_gazebo->joints[joint_name] = j;
        }

        if (HebirosGroupPhysical::findGroup(req.group_name)) {
          std::shared_ptr<HebirosGroupPhysical> group_physical =
            HebirosGroupPhysical::getGroup(req.group_name);
          group_physical->joints[joint_name] = j;
        }
      }
    }
  }

  group->joint_full_names = joint_full_names;

  if (HebirosGroupGazebo::findGroup(req.group_name)) {
    std::shared_ptr<HebirosGroupGazebo> group_gazebo =
      HebirosGroupGazebo::getGroup(req.group_name);
    group_gazebo->size = group_gazebo->joints.size();
    group->size = group_gazebo->size;
    group_gazebo->joint_full_names = joint_full_names;
  }

  if (HebirosGroupPhysical::findGroup(req.group_name)) {
    std::shared_ptr<HebirosGroupPhysical> group_physical =
      HebirosGroupPhysical::getGroup(req.group_name);
    group_physical->size = group_physical->joints.size();
    group->size = group_physical->size;
    group_physical->joint_full_names = joint_full_names;
  }

  return true;
}

bool HebirosServices::addGroupFromURDF(
  AddGroupFromURDFSrv::Request &req, AddGroupFromURDFSrv::Response &res) {

  ROS_INFO("Loaded URDF from robot_description");

  return true;
}

bool HebirosServices::split(const std::string &orig, std::string &name, std::string &family) {
  std::stringstream ss(orig);

  if (!std::getline(ss, family, '/')) {
    return false;
  }
  if (!std::getline(ss, name, '/')) {
    return false;
  }

  return true;
}

void HebirosServices::addJointChildren(std::set<std::string>& names,
  std::set<std::string>& families, std::map<std::string, std::string>& full_names,
  const urdf::Link* link) {
  for (auto& joint : link->child_joints) {

    if (joint->type != urdf::Joint::FIXED) {
      std::string name, family;

      if (split(joint->name, name, family)) {
        names.insert(name);
        families.insert(family);
        full_names[family+'/'+name] = joint->name;
      }
    }
  }

  for (auto& link_child : link->child_links) {
    addJointChildren(names, families, full_names, link_child.get());
  }
}

bool HebirosServices::addModelFromURDF(
      AddModelFromURDFSrv::Request &req, AddModelFromURDFSrv::Response &res) {

  //TODO: add to hebiros_models
  HebirosModel hebiros_model(req.model_name);

  return true;
}

bool HebirosServices::size(
  SizeSrv::Request &req, SizeSrv::Response &res, std::string group_name) {

  std::shared_ptr<HebirosGroup> group = HebirosGroup::getGroup(group_name);
  res.size = group->size;

  ROS_INFO("/hebiros/%s size=%d", group_name.c_str(), res.size);

  return true;
}

bool HebirosServices::setFeedbackFrequency(
  SetFeedbackFrequencySrv::Request &req, SetFeedbackFrequencySrv::Response &res,
  std::string group_name) {

  ROS_INFO("/hebiros/%s feedback_frequency=%d", group_name.c_str(), req.feedback_frequency);

  return true;
}

bool HebirosServices::setCommandLifetime(
  SetCommandLifetimeSrv::Request &req, SetCommandLifetimeSrv::Response &res,
  std::string group_name) {

  ROS_INFO("/hebiros/%s command_lifetime=%d", group_name.c_str(), req.command_lifetime);

  return true;
}

bool HebirosServices::sendCommandWithAcknowledgement(
  SendCommandWithAcknowledgementSrv::Request &req, 
  SendCommandWithAcknowledgementSrv::Response &res, std::string group_name) {

  return true;
}



