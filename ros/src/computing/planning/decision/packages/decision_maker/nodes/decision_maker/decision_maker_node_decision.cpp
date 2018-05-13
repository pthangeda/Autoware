#include <numeric>
#include <stdio.h>
#include <numeric>

#include <geometry_msgs/PoseStamped.h>
#include <jsk_rviz_plugins/OverlayText.h>
#include <ros/ros.h>
#include <std_msgs/String.h>
#include <tf/transform_listener.h>

#include <autoware_msgs/lane.h>
#include <autoware_msgs/traffic_light.h>

#include <cross_road_area.hpp>
#include <decision_maker_node.hpp>
#include <state_machine_lib/state.hpp>
#include <state_machine_lib/state_context.hpp>

namespace decision_maker
{
/* do not use this within callback */
bool DecisionMakerNode::waitForEvent(cstring_t &key, const bool &flag)
{
  const uint32_t monitoring_rate = 20;  // Hz

  ros::Rate loop_rate(monitoring_rate);
  while (ros::ok())
  {
    if (isEventFlagTrue(key) == flag)
    {
      break;
    }
    loop_rate.sleep();
  }
  return true;
}

bool DecisionMakerNode::waitForEvent(cstring_t &key, const bool &flag, const double &timeout_sec)
{
  const uint32_t monitoring_rate = 20;  // Hz
  ros::Rate loop_rate(monitoring_rate);

  ros::Time entry_time = ros::Time::now();

  while (ros::ok())
  {
    if (isEventFlagTrue(key) == flag)
    {
      return true;
    }
    if ((ros::Time::now() - entry_time).toSec() >= timeout_sec)
    {
      break;
    }
    loop_rate.sleep();
  }
  return false;
}
double DecisionMakerNode::calcIntersectWayAngle(const autoware_msgs::lane &laneinArea)
{
  double diff = 0.0;
  if (laneinArea.waypoints.empty())
  {
    ROS_INFO("Not inside CrossRoad");
  }
  else
  {
    const geometry_msgs::Pose InPose = laneinArea.waypoints.front().pose.pose;
    const geometry_msgs::Pose OutPose = laneinArea.waypoints.back().pose.pose;

    diff = amathutils::calcPosesAngleDiffDeg(InPose, OutPose);
  }

  return diff;
}

bool DecisionMakerNode::isLocalizationConvergence(const geometry_msgs::Point &_current_point)
{
  static std::vector<double> distances;
  static uint32_t distances_count = 0;
  static geometry_msgs::Point prev_point;

  static const int param_convergence_count_ = 10;

  bool ret = false;

  // if current point is not set, localization is failure
  if (_current_point.x == 0 && _current_point.y == 0 && _current_point.z == 0 && prev_point.x == 0 &&
      prev_point.y == 0 && prev_point.z == 0)
  {
    return ret;
  }

  distances.push_back(amathutils::find_distance(prev_point, _current_point));
  if (++distances_count > param_convergence_count_)
  {
    distances.erase(distances.begin());
    distances_count--;
    double avg_distances = std::accumulate(distances.begin(), distances.end(), 0) / distances.size();
    if (avg_distances <= param_convergence_threshold_)
    {
      ret = true;
    }
  }

  prev_point = _current_point;
  return ret;
}
bool DecisionMakerNode::isArrivedGoal()
{
  const double goal_threshold = 3.0;  // 1.0 meter
  const auto goal_point = current_status_.finalwaypoints.waypoints.back().pose.pose.position;

  if (amathutils::find_distance(goal_point, current_status_.pose.position) < goal_threshold)
  {
    if (current_status_.velocity <= 0.1)
    {
      return true;
    }
  }
  return false;
}
bool DecisionMakerNode::handleStateCmd(const uint64_t _state_num)
{
  bool _ret = false;

  /* todo  */
  /* key  */
  return _ret;
}
}
