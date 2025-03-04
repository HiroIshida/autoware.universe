cmake_minimum_required(VERSION 3.5)
project(behavior_path_planner)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

find_package(ament_cmake_auto REQUIRED)
ament_auto_find_build_dependencies()
find_package(OpenCV REQUIRED)

ament_auto_add_library(behavior_path_planner_node SHARED
  src/behavior_path_planner_node.cpp
  src/behavior_tree_manager.cpp
  src/utilities.cpp
  src/path_utilities.cpp
  src/path_shifter/path_shifter.cpp
  src/turn_signal_decider.cpp
  src/scene_module/scene_module_bt_node_interface.cpp
  src/scene_module/side_shift/side_shift_module.cpp
  src/scene_module/side_shift/util.cpp
  src/scene_module/avoidance/avoidance_module.cpp
  src/scene_module/avoidance/avoidance_utils.cpp
  src/scene_module/avoidance/debug.cpp
  src/scene_module/lane_following/lane_following_module.cpp
  src/scene_module/lane_change/lane_change_module.cpp
  src/scene_module/lane_change/util.cpp
  src/scene_module/pull_over/pull_over_module.cpp
  src/scene_module/pull_over/util.cpp
  src/scene_module/pull_out/pull_out_module.cpp
  src/scene_module/pull_out/util.cpp
)

target_include_directories(behavior_path_planner_node SYSTEM PUBLIC
  ${EIGEN3_INCLUDE_DIR}
)

target_link_libraries(behavior_path_planner_node
  ${OpenCV_LIBRARIES}
)

# workaround to allow deprecated header to build on both galactic and rolling
if(${tf2_geometry_msgs_VERSION} VERSION_LESS 0.18.0)
  target_compile_definitions(behavior_path_planner_node PUBLIC
    USE_TF2_GEOMETRY_MSGS_DEPRECATED_HEADER
  )
endif()

rclcpp_components_register_node(behavior_path_planner_node
  PLUGIN "behavior_path_planner::BehaviorPathPlannerNode"
  EXECUTABLE behavior_path_planner
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_auto_package(
  INSTALL_TO_SHARE
    config
    launch
)
