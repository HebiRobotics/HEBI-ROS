#include "hebiros_gazebo_pid.h"

namespace hebiros {
namespace sim {

// Update and return the new output command
double PidController::update(double target, double feedback, double dt, const PidGainsMsg& pid_gains, size_t gain_idx) {
  // "Disable" the controller if commands are nan
  if (std::isnan(target)) {
    return 0;
  }
  double error_p, error_i, error_d;
  error_p = target - feedback;
  error_i = elapsed_error_ + error_p;
  if (dt <= 0)
    error_d = 0;
  else
    error_d = (error_p - prev_error_) / dt;
  prev_error_ = error_p;
  elapsed_error_ = error_i;


  // TODO: store gains instead of looking them up
  // here...
  return
    pid_gains.kp[gain_idx] * error_p +
    pid_gains.ki[gain_idx] * error_i +
    pid_gains.kd[gain_idx] * error_d +
    pid_gains.feed_forward[gain_idx] * ff_scale_ * target;
}

} // namespace simulation
} // namespace hebiros
