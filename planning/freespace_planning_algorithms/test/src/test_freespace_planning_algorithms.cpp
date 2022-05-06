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

#include "freespace_planning_algorithms/astar_search.hpp"

#include <rclcpp/rclcpp.hpp>
#include <rcpputils/filesystem_helper.hpp>
#include <rosbag2_cpp/writer.hpp>

#include <std_msgs/msg/float64.hpp>

#include <gtest/gtest.h>
#include <rcutils/time.h>
#include <tf2/utils.h>

#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace fpa = freespace_planning_algorithms;

geometry_msgs::msg::Pose create_pose_msg(std::array<double, 3> pose3d)
{
  geometry_msgs::msg::Pose pose{};
  tf2::Quaternion quat{};
  quat.setRPY(0, 0, pose3d[2]);
  tf2::convert(quat, pose.orientation);
  pose.position.x = pose3d[0];
  pose.position.y = pose3d[1];
  pose.position.z = 0.0;
  return pose;
}

std_msgs::msg::Float64 create_float_msg(double val)
{
  std_msgs::msg::Float64 msg;
  msg.data = val;
  return msg;
}

nav_msgs::msg::OccupancyGrid construct_cost_map(
  int width, int height, double resolution, int n_padding)
{
  nav_msgs::msg::OccupancyGrid costmap_msg{};

  // create info
  costmap_msg.info.width = width;
  costmap_msg.info.height = height;
  costmap_msg.info.resolution = resolution;

  // create data
  costmap_msg.data.resize(width * height);

  for (int i = 0; i < n_padding; i++) {
    // fill left
    for (int j = width * i; j < width * (i + 1); j++) {
      costmap_msg.data[j] = 100.0;
    }
    // fill right
    for (int j = width * (height - n_padding + i); j < width * (height - n_padding + i + 1); j++) {
      costmap_msg.data[j] = 100.0;
    }
  }

  for (int i = 0; i < height; i++) {
    // fill bottom
    for (int j = i * width; j < i * width + n_padding; j++) {
      costmap_msg.data[j] = 100.0;
    }
    for (int j = (i + 1) * width - n_padding; j < (i + 1) * width; j++) {
      costmap_msg.data[j] = 100.0;
    }
  }
  return costmap_msg;
}

template <typename MessageT>
void add_message_to_rosbag(
  rosbag2_cpp::Writer & writer, const MessageT & message, const std::string & name,
  const std::string & type)
{
  rclcpp::SerializedMessage serialized_msg;
  rclcpp::Serialization<MessageT> serialization;
  serialization.serialize_message(&message, &serialized_msg);

  rosbag2_storage::TopicMetadata tm;
  tm.name = name;
  tm.type = type;
  tm.serialization_format = "cdr";
  writer.create_topic(tm);

  auto bag_message = std::make_shared<rosbag2_storage::SerializedBagMessage>();
  auto ret = rcutils_system_time_now(&bag_message->time_stamp);
  if (ret != RCL_RET_OK) {
    RCLCPP_ERROR(rclcpp::get_logger("saveToBag"), "couldn't assign time rosbag message");
  }

  bag_message->topic_name = tm.name;
  bag_message->serialized_data = std::shared_ptr<rcutils_uint8_array_t>(
    &serialized_msg.get_rcl_serialized_message(), [](rcutils_uint8_array_t * /* data */) {});
  writer.write(bag_message);
}

bool test_astar(
  std::array<double, 3> start, std::array<double, 3> goal, std::string dir_name,
  double maximum_turning_radius = 9.0, int turning_radius_size = 1)
{
  // set problem configuration
  fpa::VehicleShape shape{5.5, 2.75, 1.5};

  double time_limit = 10000000.0;
  double minimum_turning_radius = 9.0;
  // double maximum_turning_radius is a parameter
  // int turning_radius_size is a parameter
  int theta_size = 144;
  double curve_weight = 1.2;
  double reverse_weight = 2;
  double lateral_goal_range = 0.5;
  double longitudinal_goal_range = 2.0;
  double angle_goal_range = 6.0;
  int obstacle_threshold = 100;

  fpa::PlannerCommonParam planner_common_param{
    time_limit,
    shape,
    minimum_turning_radius,
    maximum_turning_radius,
    turning_radius_size,
    theta_size,
    curve_weight,
    reverse_weight,
    lateral_goal_range,
    longitudinal_goal_range,
    angle_goal_range,
    obstacle_threshold};

  bool only_behind_solutions = false;
  bool use_back = true;
  double distance_heuristic_weight = 1.0;
  fpa::AstarParam astar_param{only_behind_solutions, use_back, distance_heuristic_weight};

  auto astar = fpa::AstarSearch(planner_common_param, astar_param);

  auto costmap_msg = construct_cost_map(150, 150, 0.2, 10);
  astar.setMap(costmap_msg);

  rclcpp::Clock clock{RCL_SYSTEM_TIME};
  const rclcpp::Time begin = clock.now();
  bool success = astar.makePlan(create_pose_msg(start), create_pose_msg(goal));
  const rclcpp::Time now = clock.now();
  const double msec = (now - begin).seconds() * 1000.0;
  if (success) {
    std::cout << "plan success : " << msec << "[msec]" << std::endl;
  } else {
    std::cout << "plan fail : " << msec << "[msec]" << std::endl;
  }
  auto result = astar.getWaypoints();

  geometry_msgs::msg::PoseArray result_trajectory;
  for (const auto & pose : result.waypoints) {
    result_trajectory.poses.push_back(pose.pose.pose);
  }

  rcpputils::fs::remove_all(dir_name);

  rosbag2_storage::StorageOptions storage_options;
  storage_options.uri = dir_name;
  storage_options.storage_id = "sqlite3";

  rosbag2_cpp::ConverterOptions converter_options;
  converter_options.input_serialization_format = "cdr";
  converter_options.output_serialization_format = "cdr";

  rosbag2_cpp::Writer writer(std::make_unique<rosbag2_cpp::writers::SequentialWriter>());
  writer.open(storage_options, converter_options);

  add_message_to_rosbag(
    writer, create_float_msg(shape.length), "vehicle_length", "std_msgs/msg/Float64");
  add_message_to_rosbag(
    writer, create_float_msg(shape.width), "vehicle_width", "std_msgs/msg/Float64");
  add_message_to_rosbag(
    writer, create_float_msg(shape.base2back), "vehicle_base2back", "std_msgs/msg/Float64");

  add_message_to_rosbag(writer, costmap_msg, "costmap", "nav_msgs/msg/OccupancyGrid");
  add_message_to_rosbag(writer, create_pose_msg(start), "start", "geometry_msgs/msg/Pose");
  add_message_to_rosbag(writer, create_pose_msg(goal), "goal", "geometry_msgs/msg/Pose");
  add_message_to_rosbag(writer, result_trajectory, "trajectory", "geometry_msgs/msg/PoseArray");
  add_message_to_rosbag(writer, create_float_msg(msec), "elapsed_time", "std_msgs/msg/Float64");

  // backtrace the path and confirm that the entire path is collision-free
  return success && astar.hasFeasibleSolution();
}

TEST(AstarSearchTestSuite, SingleCurvature)
{
  std::vector<double> goal_xs{8., 12., 16., 26.};
  for (size_t i = 0; i < goal_xs.size(); i++) {
    std::array<double, 3> start{6., 4., 0.5 * 3.1415};
    std::array<double, 3> goal{goal_xs[i], 4., 0.5 * 3.1415};
    std::string dir_name = "/tmp/result_single" + std::to_string(i);
    EXPECT_TRUE(test_astar(start, goal, dir_name));
  }
}

TEST(AstarSearchTestSuite, MultiCurvature)
{
  std::vector<double> goal_xs{8., 12., 16., 26.};
  double maximum_turning_radius = 14.0;
  int turning_radius_size = 3;
  for (size_t i = 0; i < goal_xs.size(); i++) {
    std::array<double, 3> start{6., 4., 0.5 * 3.1415};
    std::array<double, 3> goal{goal_xs[i], 4., 0.5 * 3.1415};
    std::string dir_name = "/tmp/result_multi" + std::to_string(i);
    EXPECT_TRUE(test_astar(start, goal, dir_name, maximum_turning_radius, turning_radius_size));
  }
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
