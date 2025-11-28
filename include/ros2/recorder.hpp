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

#ifndef RECORDER_H
#define RECORDER_H

#include "rclcpp/rclcpp.hpp"
#include "corecorder/srv/recording_control.hpp"
#include "corecorder/srv/recording_status.hpp"

#include <unordered_map>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "common.hpp"
#include "writer.hpp"
#include "message_definition.hpp"
#include "create_generic_subscription.hpp"
#include "generic_subscription.hpp"

namespace recorder {
constexpr int64_t DEFAULT_MIN_QOS_DEPTH = 100;
constexpr int64_t DEFAULT_MAX_QOS_DEPTH = 3000;
constexpr char SCHEMA_ENCODING[] = "ros2msg";
constexpr char MESSAGE_ENCODING[] = "cdr";

using RecordingControl = corecorder::srv::RecordingControl;
using RecordingStatus = corecorder::srv::RecordingStatus;


class Recorder : public rclcpp::Node {
public:
  Recorder() : Node("corecorder") {
    callback_group_ = this->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    
    recording_control_srv_ = this->create_service<RecordingControl>(
      "recording_control", [this](const std::shared_ptr<RecordingControl::Request> request,
                                  std::shared_ptr<RecordingControl::Response> response) {
        if (const auto [result, msg] = recording_control(request); result) {
          response->success = true;
          response->message = msg;
          response->recording_id = request->recording_id;
          response->bucket_name = "";
        } else {
          response->success = false;
          response->message = msg;
          response->recording_id = request->recording_id;
        }
      });

    recording_status_srv_ = this->create_service<RecordingStatus>(
      "recording_status", [this](const std::shared_ptr<RecordingStatus::Request>,
      std::shared_ptr<RecordingStatus::Response> response) {
        switch (recording_status_) {
        case RECORDING_STATUS::RECORDING:
            response->status = "recording";
            break;
          case RECORDING_STATUS::FINISHED:
            response->status = "finished";
            break;
          case RECORDING_STATUS::CANCELED:
            response->status = "cancelled";
            break;
          case RECORDING_STATUS::PAUSING:
            response->status = "paused";
            break;
          default:
            response->status = "idle";
            break;
        }
      });
  }

  ~Recorder() override {
    stop_retry_flag_ = true;
    if (subscription_retry_thread_.joinable()) {
      subscription_retry_thread_.join();
    }
  }

private:
  std::tuple<bool, std::string> recording_control(
    const std::shared_ptr<RecordingControl::Request> & request) {
    if (request->command == "start") {
      if (recording_status_ == RECORDING_STATUS::CANCELED ||
        recording_status_ == RECORDING_STATUS::FINISHED) {
        return start_record(request);
      }
      return {false, "can not start record, because of illegal recording status."};
    } else if (request->command == "pause") {
      if (recording_status_ == RECORDING_STATUS::RECORDING) {
        recording_status_ = RECORDING_STATUS::PAUSING;
        return {true, "successes"};
      }
      return {false, "can not pause, because of illegal recording status."};
    } else if (request->command == "resume") {
      if (recording_status_ == RECORDING_STATUS::PAUSING) {
        recording_status_ = RECORDING_STATUS::RECORDING;
        return {true, "successes"};
      }
      return {false, "can not pause, because of illegal recording status."};
    } else if (request->command == "cancel") {
      if (recording_status_ == RECORDING_STATUS::RECORDING) {
        return cancel_record();
      }
      return {false, "can not pause, because of illegal recording status."};
    } else if (request->command == "finish") {
      if (recording_status_ == RECORDING_STATUS::RECORDING) {
        return stop_record();
      }
      return {false, "can not pause, because of illegal recording status."};
    }
    return {true, ""};
  }

  std::tuple<bool, std::string> start_record(
    const std::shared_ptr<RecordingControl::Request> & request) {
    if (request->topics.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No topics specified in request");
      return {false, "No topics specified in request"};
    }

    // Initialize writer with output file
    // Generate filename with timestamp: recording_2025-11-21_16-30-45.mcap
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);
    
    std::ostringstream oss;
    oss << "/tmp/recording_"
        << std::put_time(&tm_now, "%Y-%m-%d_%H-%M-%S")
        << ".mcap";
    std::string output_file = oss.str();
    
    writer_ = std::make_unique<Writer>(output_file, "ros2", request->compression_type, 
                                       request->compression_level);
    RCLCPP_INFO(this->get_logger(), "Initialized MCAP writer with file: %s", output_file.c_str());

    RCLCPP_INFO(this->get_logger(), "Starting record for %zu topics", request->topics.size());
    stop_retry_flag_ = true;
    if (subscription_retry_thread_.joinable()) {
      subscription_retry_thread_.join();
    }

    pending_topics_ = request->topics;
    stop_retry_flag_ = false;

    RCLCPP_INFO(this->get_logger(), "Starting subscription retry thread for %zu topics",
                pending_topics_.size());
    subscription_retry_thread_ = std::thread([this]() {
      subscription_retry_loop();
    });

    recording_status_ = RECORDING_STATUS::RECORDING;
    return {true, "start recording succeeded"};
  }

  std::tuple<bool, std::string> stop_record() {
    stop_retry_flag_ = true;
    if (subscription_retry_thread_.joinable()) {
      subscription_retry_thread_.join();
    }

    subscribers_.clear();
    pending_topics_.clear();
    writer_.reset();

    recording_status_ = RECORDING_STATUS::FINISHED;
    RCLCPP_INFO(this->get_logger(), "Successfully stopped recording");
    return {true, "stop recording success"};
  }

  std::tuple<bool, std::string> cancel_record() {
    recording_status_ = RECORDING_STATUS::CANCELED;
    return {false, "cancel recoding success"};
  }

  rclcpp::QoS get_qos_from_topic(const std::string & topic) const {
    size_t depth = 0;
    size_t reliability_reliable_endpoints_count = 0;
    size_t durability_transient_local_endpoints_count = 0;

    const auto publisher_info = this->get_publishers_info_by_topic(topic);
    for (const auto & publisher : publisher_info) {
      const auto & qos = publisher.qos_profile();
      if (qos.get_rmw_qos_profile().reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE) {
        ++reliability_reliable_endpoints_count;
      }
      if (qos.get_rmw_qos_profile().durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL) {
        ++durability_transient_local_endpoints_count;
      }
      const size_t publisher_history_depth = std::max(1ul, qos.get_rmw_qos_profile().depth);
      depth = depth + publisher_history_depth;
    }

    depth = std::max(depth, static_cast<size_t>(DEFAULT_MIN_QOS_DEPTH));
    if (depth > DEFAULT_MAX_QOS_DEPTH) {
      RCLCPP_WARN(
        this->get_logger(),
        "Limiting history depth for topic '%s' to %zu (was %zu). You may want to increase "
        "the max_qos_depth parameter value.",
        topic.c_str(), DEFAULT_MAX_QOS_DEPTH, depth);
      depth = DEFAULT_MAX_QOS_DEPTH;
    } else {
      RCLCPP_INFO(this->get_logger(), "Using QoS depth %zu for topic '%s'", depth, topic.c_str());
    }

    rclcpp::QoS qos{rclcpp::KeepLast(depth)};

    if (reliability_reliable_endpoints_count == publisher_info.size()) {
      qos.reliable();
    } else {
      if (reliability_reliable_endpoints_count > 0) {
        RCLCPP_INFO(
          this->get_logger(),
          "Some, but not all, publishers on topic '%s' are offering QoSReliabilityPolicy.RELIABLE."
          "Falling back to QoSReliabilityPolicy.BEST_EFFORT as it will connect to all publishers",
          topic.c_str());
      }
      qos.best_effort();
    }

    // If all endpoints are transient_local, ask for transient_local
    if (durability_transient_local_endpoints_count == publisher_info.size()) {
      qos.transient_local();
    } else {
      if (durability_transient_local_endpoints_count > 0) {
        RCLCPP_INFO(this->get_logger(), "Some, but not all, publishers on topic '%s' are offering"
                    "QoSDurabilityPolicy.TRANSIENT_LOCAL. Falling back to "
                    "QoSDurabilityPolicy.VOLATILE as it will connect to all publishers",
                    topic.c_str());
      }
      qos.durability_volatile();
    }
    return qos;
  }


  void subscription_retry_loop() {
    while (!stop_retry_flag_ && !pending_topics_.empty()) {
      RCLCPP_INFO(this->get_logger(), "Retrying subscription for %zu pending topics",
                  pending_topics_.size());

      auto it = pending_topics_.begin();
      while (it != pending_topics_.end() && !stop_retry_flag_) {
        const std::string & topic_name = *it;

        try {
          const auto topics_info = this->get_topic_names_and_types();
          auto topic_iter = topics_info.find(topic_name);

          if (topic_iter == topics_info.end() || topic_iter->second.empty()) {
            RCLCPP_DEBUG(this->get_logger(), "Topic '%s' not available yet, will retry",
                         topic_name.c_str());
            ++it;
            continue;
          }

          const auto publisher_info = this->get_publishers_info_by_topic(topic_name);
          if (publisher_info.empty()) {
            RCLCPP_DEBUG(this->get_logger(), "Topic '%s' has no publishers yet, will retry",
                         topic_name.c_str());
            ++it;
            continue;
          }

          const std::string & topic_type = topic_iter->second[0];
          auto [format, schema] =
            message_definition_cache_.get_full_msg_text(topic_type);
          writer_->add_schema(topic_type, SCHEMA_ENCODING, schema);
          writer_->add_channel(topic_name, topic_type, MESSAGE_ENCODING);

          auto qos = get_qos_from_topic(topic_name);
#ifdef ROS2_VERSION_FOXY
          auto subscription = create_generic_subscription(
            this->get_node_topics_interface(), topic_name, topic_type, qos,
            [this, topic_name](std::shared_ptr<const rclcpp::SerializedMessage> msg,
                               uint64_t timestamp) {
              if (recording_status_ == RECORDING_STATUS::PAUSING) {
                return;
              }
              const auto & rcl_msg = msg->get_rcl_serialized_message();
              const auto status = writer_->write_message(
                reinterpret_cast<const std::byte*>(rcl_msg.buffer),
                rcl_msg.buffer_length, topic_name, timestamp);
              if (!status.ok()) {
                RCLCPP_WARN(this->get_logger(), "Failed to write [%s] message: '%s'",
                            topic_name.c_str(), status.message.c_str());
              }
            }
          );
# else
          rclcpp::SubscriptionEventCallbacks event_callbacks;
          event_callbacks.incompatible_qos_callback =
              [this, topic_name, topic_type](const rclcpp::QOSRequestedIncompatibleQoSInfo &) {
                COLOG_INFO("Incompatible subscriber QoS settings for topic \"%s\" (%s)",
                    topic_name.c_str(), topic_type.c_str());
          };

          rclcpp::SubscriptionOptions subscription_options;
          subscription_options.event_callbacks = event_callbacks;
          subscription_options.callback_group = callback_group_;

          auto subscription = this->create_generic_subscription(
              topic_name, topic_type, qos,
              [this, topic_name, topic_type](std::shared_ptr<const rclcpp::SerializedMessage> msg) {
                if (recording_status_ == RECORDING_STATUS::PAUSING) {
                  return;
                }
                const uint64_t recv_time = this->now().nanoseconds();
                const auto & rcl_msg = msg->get_rcl_serialized_message();
                const auto status = writer_->write_message(
                  reinterpret_cast<const std::byte*>(rcl_msg.buffer),
                  rcl_msg.buffer_length, topic_name, recv_time);
                if (!status.ok()) {
                  RCLCPP_WARN(this->get_logger(), "Failed to write [%s] message: '%s'",
                              topic_name.c_str(), status.message.c_str());
                }
              },
              subscription_options);
#endif
          subscribers_[topic_name] = subscription;
          RCLCPP_INFO(this->get_logger(), "Successfully subscribed to topic '%s' with type '%s'",
                      topic_name.c_str(), topic_type.c_str());
          it = pending_topics_.erase(it);
        } catch (const std::exception & e) {
          RCLCPP_WARN(this->get_logger(), "Failed to subscribe to topic '%s': %s, will retry",
                      topic_name.c_str(), e.what());
          ++it;
        }
      }

      if (!pending_topics_.empty() && !stop_retry_flag_) {
        std::string topics_str;
        for (size_t i = 0; i < pending_topics_.size(); ++i) {
          if (i > 0)
            topics_str += ", ";
          topics_str += pending_topics_[i];
        }

        RCLCPP_INFO(this->get_logger(), "Still waiting for %zu topics: [%s]",
                    pending_topics_.size(), topics_str.c_str());
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }

    if (pending_topics_.empty()) {
      RCLCPP_INFO(this->get_logger(), "All topics successfully subscribed!");
    } else if (stop_retry_flag_) {
      RCLCPP_INFO(this->get_logger(), "Subscription retry stopped");
    }
  }

  RECORDING_STATUS recording_status_ = RECORDING_STATUS::FINISHED;
  std::unique_ptr<Writer> writer_;
  MessageDefinitionCache message_definition_cache_;

  // Callback group for parallel callback execution
  rclcpp::CallbackGroup::SharedPtr callback_group_;

  // Store subscribers for all topics
  std::unordered_map<std::string, rclcpp::SubscriptionBase::SharedPtr> subscribers_;

  // For retry subscription logic
  std::vector<std::string> pending_topics_;
  std::thread subscription_retry_thread_;
  std::atomic<bool> stop_retry_flag_{false};

  rclcpp::Service<RecordingControl>::SharedPtr recording_control_srv_;
  rclcpp::Service<RecordingStatus>::SharedPtr recording_status_srv_;
};
}  // namespace recorder

#endif  //RECORDER_H
