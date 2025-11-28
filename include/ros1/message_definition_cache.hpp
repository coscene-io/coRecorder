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

#ifndef MESSAGE_DEFINITION_CACHE_HPP
#define MESSAGE_DEFINITION_CACHE_HPP
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <cctype>

#include "ros/ros.h"
#include "ros/package.h"

namespace recorder {
#define MESSAGE_SPLIT_STRING "================================================================================\n"

class MsgDefinitionCache {
public:
  std::string getMessageDefinition(const std::string & type) {
    return resolveFullText(type, true);  // true = is root message
  }

private:
  static std::string readFile(const std::string & path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  static bool findMsgFile(const std::string & pkg, const std::string & msg,
                          std::string & out_path) {
    std::string pkg_path = ros::package::getPath(pkg);
    if (pkg_path.empty())
      return false;

    std::string file_path = pkg_path + "/msg/" + msg + ".msg";
    std::ifstream f(file_path);
    if (!f.good())
      return false;

    out_path = file_path;
    return true;
  }

  static bool isPrimitiveType(const std::string & type) {
    // ROS1 primitive types
    static const std::unordered_set<std::string> primitive_types = {
      "bool", "int8", "uint8", "int16", "uint16", "int32", "uint32",
      "int64", "uint64", "float32", "float64", "string", "time", "duration"
    };
    return primitive_types.count(type) > 0;
  }

  std::string resolveFullText(const std::string & type, bool is_root = false) {
    std::string content;
    
    if (message_definition_cache_.count(type)) {
      content = message_definition_cache_[type];
    } else {
      auto slash = type.find('/');
      if (slash == std::string::npos)
        return "";

      std::string pkg = type.substr(0, slash);
      std::string msg = type.substr(slash + 1);

      std::string msg_file;
      if (!findMsgFile(pkg, msg, msg_file))
        return "";

      std::string text = readFile(msg_file);

      std::stringstream output;
      output << text << "\n";

      std::regex field_regex(
        R"(([a-zA-Z_][a-zA-Z0-9_]*(?:/[a-zA-Z_][a-zA-Z0-9_]*)?)(\[\])?\s+([a-zA-Z_][a-zA-Z0-9_]*))");

      std::stringstream ss(text);
      std::string line;

      while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#')
          continue;

        if (size_t comment_pos = line.find('#'); comment_pos != std::string::npos) {
          line = line.substr(0, comment_pos);
        }

        while (!line.empty() && std::isspace(line.back())) {
          line.pop_back();
        }

        if (line.empty())
          continue;

        if (std::smatch match; std::regex_search(line, match, field_regex)) {
          std::string sub_type = match[1];
          if (isPrimitiveType(sub_type)) {
            continue;
          }

          std::string full_type;
          if (sub_type == "Header") {
            full_type = "std_msgs/Header";
          } else if (sub_type.find('/') != std::string::npos) {
            full_type = sub_type;
          } else {
            full_type = pkg + "/" + sub_type;
          }
          output << resolveFullText(full_type, false);
        }
      }
      content = output.str();
      message_definition_cache_[type] = content;
    }

    if (is_root) {
      return content;
    } else {
      std::stringstream result;
      result << MESSAGE_SPLIT_STRING << "MSG: " << type << "\n" << content;
      return result.str();
    }
  }


  std::unordered_map<std::string, std::string> message_definition_cache_;
};
}  // namespace recorder

#endif  // MESSAGE_DEFINITION_CACHE_HPP
