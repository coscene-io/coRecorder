/// Copyright 2025 coScene
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

#ifndef CONFIG_H_
#define CONFIG_H_

#include <cstring>
#include <string>
#include <sys/stat.h>
enum struct RECORDING_STATUS
{
  RECORDING,
  PAUSING,
  CANCELED,
  FINISHED,
};


inline void create_directory(const std::string & path)
{
  std::string dir_path = path;
  const size_t last_slash = path.find_last_of('/');
  if (last_slash != std::string::npos) {
    dir_path = path.substr(0, last_slash);
  }

  struct stat st;
  if (stat(dir_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    return;
  }

  size_t pos = 0;
  while ((pos = path.find('/', pos + 1)) != std::string::npos) {
    std::string sub_dir = path.substr(0, pos);
    if (!sub_dir.empty()) {
      const int ret = mkdir(sub_dir.c_str(), 0755);
      if (ret == -1 && errno != EEXIST) {
        std::string error_msg = "Failed to create log directory: " + path +
          " (errno: " + std::to_string(errno) +
          ", " + std::strerror(errno) + ")";
        throw std::runtime_error(error_msg);
      }
    }
  }
}


#endif  // CONFIG_H_
