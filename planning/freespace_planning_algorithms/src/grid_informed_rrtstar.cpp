// Copyright 2021 Tier IV, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "freespace_planning_algorithms/grid_informed_rrtstar.h"

namespace freespace_planning_algorithms
{
GridInformedRRTStar::GridInformedRRTStar(
  const PlannerCommonParam & planner_common_param, const VehicleShape & original_vehicle_shape,
  const RRTStarParam & rrtstar_param)
: AbstractPlanningAlgorithm(
    planner_common_param,
    VehicleShape{
      original_vehicle_shape.length + 2 * rrtstar_param_.margin,
      original_vehicle_shape.width + 2 * rrtstar_param_.margin,
      original_vehicle_shape.base2back + rrtstar_param_.margin}),
  rrtstar_param_(rrtstar_param),
  original_vehicle_shape_(original_vehicle_shape)
{
  if (rrtstar_param_.margin <= 0) {
    throw std::invalid_argument("rrt's collision margin must be greater than 0");
  }
  if (planner_common_param_.maximum_turning_radius != planner_common_param.minimum_turning_radius) {
    throw std::invalid_argument("Currently supports only single radius in rrtstar.");
  }
}

bool GridInformedRRTStar::makePlan(
  const geometry_msgs::msg::Pose & start_pose, const geometry_msgs::msg::Pose & goal_pose)
{
  const rclcpp::Time begin = rclcpp::Clock(RCL_ROS_TIME).now();

  start_pose_ = global2local(costmap_, start_pose);
  goal_pose_ = global2local(costmap_, goal_pose);

  const auto pose2index = [&](const rrtstar::Pose & pose) {
    const size_t index_x = pose.x / costmap_.info.resolution;
    const size_t index_y = pose.y / costmap_.info.resolution;
    const size_t index_theta = discretizeAngle(pose.yaw, planner_common_param_.theta_size);
    return IndexXYT{index_x, index_y, index_theta};
  };

  const auto is_obstacle_free = [&](const rrtstar::Pose & pose) {
    auto && index = pose2index(pose);
    return !detectCollision(index);
  };

  const auto rospose2rrtpose = [](const geometry_msgs::msg::Pose & pose_msg) {
    return rrtstar::Pose{
      pose_msg.position.x, pose_msg.position.y, tf2::getYaw(pose_msg.orientation)};
  };

  const rrtstar::Pose lo{0, 0, 0};
  const rrtstar::Pose hi{
    costmap_.info.resolution * costmap_.info.width, costmap_.info.resolution * costmap_.info.height,
    M_PI};
  const double radius = planner_common_param_.minimum_turning_radius;
  const auto cspace = rrtstar::CSpace(lo, hi, radius, is_obstacle_free);
  const auto x_start = rospose2rrtpose(start_pose_);
  const auto x_goal = rospose2rrtpose(goal_pose_);
  const bool is_informed = true;  // always better
  const double collision_check_resolution = rrtstar_param_.margin * 2;
  auto algo = rrtstar::RRTStar(
    x_start, x_goal, rrtstar_param_.mu, collision_check_resolution, is_informed, cspace);
  while (true) {
    const rclcpp::Time now = rclcpp::Clock(RCL_ROS_TIME).now();
    const double msec = (now - begin).seconds() * 1000.0;
    if (msec > planner_common_param_.time_limit) {
      break;
    }

    algo.extend();
    if (!rrtstar_param_.enable_update && algo.isSolutionFound()) {
      break;
    }
  }

  if (!algo.isSolutionFound()) {
    return false;
  }
  const auto waypoints = algo.sampleSolutionWaypoints(costmap_.info.resolution);
  setRRTPath(waypoints);
  solution_cost_ = algo.getSolutionCost();
  return true;
}

bool GridInformedRRTStar::hasObstacleOnTrajectory(
  const geometry_msgs::msg::PoseArray & trajectory) const
{
  for (const auto & pose : trajectory.poses) {
    const auto pose_local = global2local(costmap_, pose);
    const auto base_index = pose2index(costmap_, pose_local, planner_common_param_.theta_size);

    if (detectCollision(base_index)) {
      return true;
    }
  }
  return false;
}

void GridInformedRRTStar::setRRTPath(const std::vector<rrtstar::Pose> & waypoints)
{
  std_msgs::msg::Header header;
  header.stamp = rclcpp::Clock(RCL_ROS_TIME).now();
  header.frame_id = costmap_.header.frame_id;

  waypoints_.header = header;
  waypoints_.waypoints.clear();

  for (size_t i = 0; i < waypoints.size(); ++i) {
    auto & pt = waypoints.at(i);
    geometry_msgs::msg::Pose pose_local;
    pose_local.position.x = pt.x;
    pose_local.position.y = pt.y;
    pose_local.position.z = 0.0;
    tf2::Quaternion quat;
    quat.setRPY(0, 0, pt.yaw);
    tf2::convert(quat, pose_local.orientation);

    geometry_msgs::msg::PoseStamped pose;
    pose.pose = local2global(costmap_, pose_local);
    pose.header = header;
    PlannerWaypoint pw;
    if (i == waypoints.size() - 1) {
      pw.is_back = waypoints_.waypoints.at(i - 1).is_back;
    } else {
      const auto & pt_now = waypoints.at(i);
      const auto & pt_next = waypoints.at(i + 1);
      const double inpro =
        cos(pt_now.yaw) * (pt_next.x - pt_now.x) + sin(pt_now.yaw) * (pt_next.y - pt_now.y);
      pw.is_back = (inpro < 0.0);
    }
    pw.pose = pose;
    waypoints_.waypoints.push_back(pw);
  }
}

}  // namespace freespace_planning_algorithms
