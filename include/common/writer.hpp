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
#include <mutex>

#include "logger.hpp"
#include "mcap/writer.hpp"

namespace recorder {
class Writer {
public:
  explicit Writer(const std::string & filename, const std::string & profile,
                  const std::string & compress_type = "zstd", int32_t compress_level = 0) {
    auto opt = mcap::McapWriterOptions{profile};
    
    opt.chunkSize = 1 * 1024 * 1024;
    opt.noChunkCRC = true;
    opt.noChunking = false;
    
    if (compress_type == "zstd") {
      opt.compression = mcap::Compression::Zstd;
    } else if (compress_type == "lz4") {
      opt.compression = mcap::Compression::Lz4;
    } else {
      opt.compression = mcap::Compression::None;
    }

    switch (compress_level) {
      case 0:
        opt.compressionLevel = mcap::CompressionLevel::Fastest;
        break;
      case 1:
        opt.compressionLevel = mcap::CompressionLevel::Fast;
        break;
      case 2:
        opt.compressionLevel = mcap::CompressionLevel::Default;
        break;
      case 3:
        opt.compressionLevel = mcap::CompressionLevel::Slow;
        break;
      case 4:
        opt.compressionLevel = mcap::CompressionLevel::Slowest;
        break;
      default:
        COLOG_WARN("unsupported compression level: %d", compress_level);
        opt.compressionLevel = mcap::CompressionLevel::Default;
        break;
    }

    auto status = writer_.open(filename, opt);
    if (!status.ok()) {
      COLOG_ERROR("Failed to open MCAP file: %s", status.message.c_str());
    }
  }

  explicit Writer(std::ostream & stream) {
    writer_.open(stream, mcap::McapWriterOptions{""});
  }

  ~Writer() {
    std::lock_guard<std::mutex> lock(mutex_);
    writer_.close();
  }

  void add_schema(const std::string & msg_type, const std::string & encoding,
                  const std::string & msg_def) {
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel_map_.count(topic) != 0) {
      return;
    }
    mcap::Channel channel;
    channel.id = channel_map_.size() + 1;
    channel.topic = topic;
    channel.messageEncoding = encoding;
    channel.schemaId = schema_map_[msg_def].id;

    channel_map_[topic] = channel;
    channel_id_cache_[topic] = channel.id;
    writer_.addChannel(channel);
  }

  mcap::Status write_message(const std::byte* msg_data, const uint64_t msg_size,
                             const std::string & topic, const uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    mcap::Message mcapMsg;
    mcapMsg.logTime = timestamp;
    mcapMsg.publishTime = timestamp;
    mcapMsg.data = msg_data;
    mcapMsg.dataSize = msg_size;

    auto it = channel_id_cache_.find(topic);
    if (it != channel_id_cache_.end()) {
      mcapMsg.channelId = it->second;
    } else {
      mcapMsg.channelId = channel_map_[topic].id;
    }
    return writer_.write(mcapMsg);
  }

private:
  std::unordered_map<std::string, mcap::Schema> schema_map_;
  std::unordered_map<std::string, mcap::Channel> channel_map_;
  std::unordered_map<std::string, uint16_t> channel_id_cache_;  // Fast lookup cache
  mcap::McapWriter writer_;
  std::mutex mutex_;  // Protect concurrent access to writer
};
}

#endif  // WRITER_HPP_
