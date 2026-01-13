// Copyright 2024 coScene
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

#ifndef GENERIC_SUBSCRIPTION_HPP_
#define GENERIC_SUBSCRIPTION_HPP_

#ifdef ROS2_VERSION_FOXY
#include <memory>
#include <string>

#include "rclcpp/any_subscription_callback.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp/subscription.hpp"

namespace recorder
{
rcl_subscription_options_t get_subscription_options(const rclcpp::QoS & qos)
{
  auto options = rcl_subscription_get_default_options();
  options.qos = qos.get_rmw_qos_profile();
  return options;
}
/**
 * This class is an implementation of an rclcpp::Subscription for serialized messages whose topic
 * is not known at compile time (hence templating does not work).
 *
 * It does not support intra-process handling
 */
class GenericSubscription : public rclcpp::SubscriptionBase
{
public:
  using SharedPtr = std::shared_ptr<GenericSubscription>;

  /**
   * Constructor. In order to properly subscribe to a topic, this subscription needs to be added to
   * the node_topic_interface of the node passed into this constructor.
   *
   * \param node_base NodeBaseInterface pointer used in parts of the setup.
   * \param ts Type support handle
   * \param topic_name Topic name
   * \param callback Callback for new messages of serialized form
   */
  GenericSubscription(
    rclcpp::node_interfaces::NodeBaseInterface * node_base,
    const rosidl_message_type_support_t & ts,
    std::string topic_name,
    std::string topic_type,
    const rclcpp::QoS & qos,
    std::function<void(std::shared_ptr<rclcpp::SerializedMessage>, uint64_t timestamp)> callback)
: SubscriptionBase(node_base, ts, topic_name, get_subscription_options(qos), true),
  default_allocator_(rcutils_get_default_allocator()),
  callback_(std::move(callback)),
  qos_(qos),
  last_frame_timestamp_(0),
  message_type_(std::move(topic_type)),
  topic_name_(std::move(topic_name))
  {
  };

  GenericSubscription(const GenericSubscription &) = delete;

  GenericSubscription & operator=(const GenericSubscription &) = delete;

  // Same as create_serialized_message() as the subscription is to serialized_messages only
  std::shared_ptr<void> create_message() override {
    return create_serialized_message();
  };

  std::shared_ptr<rclcpp::SerializedMessage> create_serialized_message() override{
    return borrow_serialized_message(0);
  };

  void handle_message(
  std::shared_ptr<void> & message, const rclcpp::MessageInfo & message_info) override{
    auto typed_message = std::static_pointer_cast<rclcpp::SerializedMessage>(message);
    callback_(
      typed_message,
      static_cast<uint64_t>(message_info.get_rmw_message_info().source_timestamp));
  };

  void handle_loaned_message(
  void * loaned_message, const rclcpp::MessageInfo & message_info) override{
    (void) loaned_message;
    (void) message_info;
  };

  // Same as return_serialized_message() as the subscription is to serialized_messages only
  void return_message(std::shared_ptr<void> & message) override{
    auto typed_message = std::static_pointer_cast<rclcpp::SerializedMessage>(message);
    return_serialized_message(typed_message);
  };

  void return_serialized_message(std::shared_ptr<rclcpp::SerializedMessage> & message) override{
    message.reset();
  };

  // Provide a const reference to the QoS Profile used to create this subscription.
  const rclcpp::QoS & qos_profile() const
  {
    return qos_;
  };

private:
  static std::shared_ptr<rclcpp::SerializedMessage> borrow_serialized_message(size_t capacity) {
    return std::make_shared<rclcpp::SerializedMessage>(capacity);
  };

  rcutils_allocator_t default_allocator_;
  std::function<void(std::shared_ptr<rclcpp::SerializedMessage>, uint64_t timestamp)> callback_;
  const rclcpp::QoS qos_;
  int64_t last_frame_timestamp_;
  std::string message_type_;
  std::string topic_name_;
  bool use_down_sample_{};
};
}  // namespace recorder
#endif

#endif  // GENERIC_SUBSCRIPTION_HPP_
