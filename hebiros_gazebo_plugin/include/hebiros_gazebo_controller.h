#pragma once

#include "ros/ros.h"
#include "hebiros_gazebo_group.h"
#include "hebiros_gazebo_joint.h"

using namespace hebiros;

class HebirosGazeboController {

public:

  HebirosGazeboController() = default;
  
  static double ComputeForce(std::shared_ptr<HebirosGazeboGroup> hebiros_group, 
    std::shared_ptr<HebirosGazeboJoint> hebiros_joint,
    double position, double velocity, double effort, const ros::Duration& iteration_time);
  
  static void SetSettings(std::shared_ptr<HebirosGazeboGroup> hebiros_group, 
    std::shared_ptr<HebirosGazeboJoint> hebiros_joint);
  
  static void ChangeSettings(std::shared_ptr<HebirosGazeboGroup> hebiros_group, 
    std::shared_ptr<HebirosGazeboJoint> hebiros_joint);
  
  static void SetDefaultGains(std::shared_ptr<HebirosGazeboGroup> hebiros_group, 
    std::shared_ptr<HebirosGazeboJoint> hebiros_joint);
  
  static double Clip(double x, double low, double high);

};
