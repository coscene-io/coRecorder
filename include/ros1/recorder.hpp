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

#ifndef RECORDER_HPP
#define RECORDER_HPP

#include <thread>
#include "ros/ros.h"
#include <topic_tools/shape_shifter.h>

#include "common.hpp"
#include "writer.hpp"
#include "logger.hpp"
#include "message_definition_cache.hpp"

#include "corecorder/RecordingControl.h"
#include "corecorder/RecordingStatus.h"


namespace recorder {
using RecordingControl = corecorder::RecordingControl;
using RecordingStatus = corecorder::RecordingStatus;

constexpr char SCHEMA_ENCODING[] = "ros1msg";
constexpr char MESSAGE_ENCODING[] = "ros1";
constexpr uint32_t SUBSCRIPTION_QUEUE_LENGTH = 10;

class Recorder {
public:
  Recorder() : nh_("~") {
    Logger::getInstance().set_log_level("debug");
    recording_control_srv_ = nh_.advertiseService<
      RecordingControl::Request, RecordingControl::Response>(
      "/recording_control",
      [this](RecordingControl::Request & req, RecordingControl::Response & res) {
        COLOG_INFO("service [ recording_control ] was called");
        if (const auto [result, msg] = recording_control(req); result) {
          res.success = true;
          res.message = msg;
          res.recording_id = req.recording_id;
          res.bucket_name = "";
        } else {
          res.success = false;
          res.message = msg;
          res.recording_id = req.recording_id;
        }
        return true;
      });

    recording_status_srv_ = nh_.advertiseService<
      RecordingStatus::Request, RecordingStatus::Response>(
      "/recording_status",
      [this](RecordingStatus::Request &, RecordingStatus::Response & res) {
        COLOG_INFO("service [ recording_status ] was called");
        switch (recording_status_) {
          case RECORDING_STATUS::RECORDING:
            res.status = "recording";
            break;
          case RECORDING_STATUS::FINISHED:
            res.status = "finished";
            break;
          case RECORDING_STATUS::CANCELED:
            res.status = "cancelled";
            break;
          case RECORDING_STATUS::PAUSING:
            res.status = "paused";
            break;
          default:
            res.status = "idle";
            break;
        }
        return true;
      });
  }

  ~Recorder() {
    stop_retry_flag_ = true;
    if (subscription_retry_thread_.joinable()) {
      subscription_retry_thread_.join();
    }
  }

private:
  std::tuple<bool, std::string> recording_control(const RecordingControl::Request & request) {
    if (request.command == "start") {
      if (recording_status_ == RECORDING_STATUS::CANCELED ||
        recording_status_ == RECORDING_STATUS::FINISHED) {
        return start_record(request);
      }
      return {false, "can not start recording, because of illegal recording status."};
    } else if (request.command == "pause") {
      if (recording_status_ == RECORDING_STATUS::RECORDING) {
        recording_status_ = RECORDING_STATUS::PAUSING;
        return {true, "successes"};
      }
      return {false, "can not pause recording, because of illegal recording status."};
    } else if (request.command == "resume") {
      if (recording_status_ == RECORDING_STATUS::PAUSING) {
        recording_status_ = RECORDING_STATUS::RECORDING;
        return {true, "successes"};
      }
      return {false, "can not resume recording, because of illegal recording status."};
    } else if (request.command == "cancel") {
      if (recording_status_ == RECORDING_STATUS::RECORDING) {
        return cancel_record();
      }
      return {false, "can not cancel recording, because of illegal recording status."};
    } else if (request.command == "finish") {
      if (recording_status_ == RECORDING_STATUS::RECORDING) {
        return stop_record();
      }
      return {false, "can not finish recording, because of illegal recording status."};
    }
    return {true, ""};
  }

  std::tuple<bool, std::string> start_record(const RecordingControl::Request & request) {
    if (request.topics.empty()) {
      COLOG_INFO("No topics specified in request");
      return {false, "No topics specified in request"};
    }

    std::string output_file = "/tmp/recording.mcap";
    writer_ = std::make_unique<Writer>(output_file, "ros1", request.compression_type,
                                       request.compression_level);
    COLOG_INFO("Initialized MCAP writer with file: %s", output_file.c_str());

    COLOG_INFO("Starting record for %zu topics", request.topics.size());
    start_subscription_retry_thread(request.topics);
    recording_status_ = RECORDING_STATUS::RECORDING;
    return {true, "start recording succeeded"};
  }

  void start_subscription_retry_thread(const std::vector<std::string> & topics) {
    stop_retry_flag_ = true;
    if (subscription_retry_thread_.joinable()) {
      subscription_retry_thread_.join();
    }

    pending_topics_ = topics;
    stop_retry_flag_ = false;

    COLOG_INFO("Starting subscription retry thread for %zu topics", pending_topics_.size());
    subscription_retry_thread_ = std::thread([this]() {
      subscription_retry_loop();
    });
  }

  void subscription_retry_loop() {
    while (!stop_retry_flag_ && !pending_topics_.empty()) {
      auto it = pending_topics_.begin();
      while (it != pending_topics_.end() && !stop_retry_flag_) {
        const std::string & topic_name = *it;
        try {
          std::vector<ros::master::TopicInfo> topic_infos;
          if (!ros::master::getTopics(topic_infos)) {
            ROS_WARN("Failed to retrieve published topics from ROS master.");
            return;
          }
          auto topic_iter = std::find_if(topic_infos.begin(), topic_infos.end(),
                                         [&](const ros::master::TopicInfo & topic) {
                                           return topic.name == topic_name;
                                         });
          if (topic_iter == topic_infos.end()) {
            ++it;
            continue;
          }

          const std::string topic_type = topic_iter->datatype;
          const auto msg_definition = message_definition_cache_.getMessageDefinition(topic_type);
          writer_->add_schema(topic_type, SCHEMA_ENCODING, msg_definition);
          writer_->add_channel(topic_name, topic_type, MESSAGE_ENCODING);
          auto subscription = nh_.subscribe<topic_tools::ShapeShifter>(
            topic_name, SUBSCRIPTION_QUEUE_LENGTH,
            [this, topic_name](const topic_tools::ShapeShifter::ConstPtr & msg) {
              if (writer_) {
                const size_t len = msg->size();
                std::vector<uint8_t> buffer(len);
                ros::serialization::OStream stream(buffer.data(), len);
                msg->write(stream);

                const ros::Time t = ros::Time::now();
                const uint64_t now_ns = static_cast<uint64_t>(t.sec) * 1000000000ULL
                  + static_cast<uint64_t>(t.nsec);

                if (const auto status = writer_->write_message(
                  reinterpret_cast<const std::byte*>(buffer.data()),
                  msg->size(),
                  topic_name,
                  now_ns); !status.ok()) {
                  ROS_WARN("Failed to write [%s] message: '%s'",
                           topic_name.c_str(), status.message.c_str());
                }
              }
            }
          );
          subscribers_[topic_name] = subscription;
          it = pending_topics_.erase(it);
        } catch (const std::exception & e) {
          COLOG_INFO("Failed to subscribe to topic '%s': %s, will retry",
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
        COLOG_INFO("Still waiting for %zu topics: [%s]", pending_topics_.size(),
                   topics_str.c_str());
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
    if (pending_topics_.empty()) {
      COLOG_INFO("All topics successfully subscribed!");
    } else if (stop_retry_flag_) {
      COLOG_INFO("Subscription retry stopped");
    }
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
    COLOG_INFO("Successfully stopped recording");
    return {true, "stop recording success"};
  }

  std::tuple<bool, std::string> cancel_record() {
    recording_status_ = RECORDING_STATUS::CANCELED;
    return {false, "cancel recoding success"};
  }

  ros::NodeHandle nh_;
  std::unique_ptr<Writer> writer_;

  ros::ServiceServer recording_control_srv_;
  ros::ServiceServer recording_status_srv_;

  RECORDING_STATUS recording_status_ = RECORDING_STATUS::FINISHED;

  std::unordered_map<std::string, ros::Subscriber> subscribers_;
  // ros_babel_fish::IntegratedDescriptionProvider ros_type_info_provider_;
  MsgDefinitionCache message_definition_cache_;

  std::vector<std::string> pending_topics_;
  std::thread subscription_retry_thread_;
  std::atomic<bool> stop_retry_flag_{false};
};
}  // namespace recorder
#endif  // RECORDER_HPP
