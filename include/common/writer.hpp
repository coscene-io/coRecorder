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

#ifndef WRITER_HPP_
#define WRITER_HPP_

#include <cstring>
#include <unordered_map>
#include "mcap/writer.hpp"

namespace recorder {
class Writer {
public:
  explicit Writer(const std::string & filename) {
    auto status = writer_.open(filename, mcap::McapWriterOptions{""});
  }

  explicit Writer(std::ostream & stream) {
    writer_.open(stream, mcap::McapWriterOptions{""});
  }

  ~Writer() {
    writer_.close();
  }

  void add_schema(const std::string & msg_type, const std::string & encoding,
                  const std::string & msg_def) {
    if (schema_map_.count(msg_type) != 0) {
      return;
    }
    mcap::Schema schema;
    schema.name = msg_type;
    schema.id = schema_map_.size() + 1;
    schema.encoding = encoding;

    std::vector<std::byte> bytes(msg_def.size());
    std::memcpy(bytes.data(), msg_def.data(), msg_def.size());
    schema.data = bytes;

    schema_map_[msg_type] = schema;
    writer_.addSchema(schema);
  }

  void add_channel(const std::string & topic, const std::string & msg_def,
                   const std::string & encoding) {
    if (channel_map_.count(topic) != 0) {
      return;
    }
    mcap::Channel channel;
    channel.id = channel_map_.size() + 1;
    channel.topic = topic;
    channel.messageEncoding = encoding;
    channel.schemaId = schema_map_[msg_def].id;

    channel_map_[topic] = channel;
    writer_.addChannel(channel);
  }

  mcap::Status write_message(const std::byte* msg_data, const uint64_t msg_size,
                             const std::string & topic, const uint64_t timestamp) {
    mcap::Message mcapMsg;
    mcapMsg.channelId = channel_map_[topic].id;
    mcapMsg.logTime = timestamp;
    mcapMsg.publishTime = timestamp;
    mcapMsg.data = msg_data;
    mcapMsg.dataSize = msg_size;

    return writer_.write(mcapMsg);
  }

private:
  std::unordered_map<std::string, mcap::Schema> schema_map_;
  std::unordered_map<std::string, mcap::Channel> channel_map_;
  mcap::McapWriter writer_;
};
}

#endif  // WRITER_HPP_
