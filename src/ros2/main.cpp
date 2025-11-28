// Copyright 2025 coScene
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

#define MCAP_IMPLEMENTATION

#include <memory>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include "../../include/ros2/recorder.hpp"
#include "rclcpp/executors/multi_threaded_executor.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  RCLCPP_INFO(rclcpp::get_logger("main"), "recorder started");
  const auto recorder = std::make_shared<recorder::Recorder>();
  
  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);
  executor.add_node(recorder);
  executor.spin();
  
  rclcpp::shutdown();
  return 0;
}
