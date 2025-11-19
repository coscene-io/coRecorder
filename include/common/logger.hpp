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

#ifndef UTILS__LOGGER_HPP_
#define UTILS__LOGGER_HPP_

#include <string>
#include <fstream>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <vector>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <common.hpp>


enum class LogLevel {
  DEBUG = 1,
  INFO = 2,
  WARN = 4,
  ERROR = 8
};

class Logger {
public:
  static Logger& getInstance() {
    static Logger instance;
    return instance;
  }

  Logger()
    : current_level_(LogLevel::INFO) {
    const char* home = std::getenv("HOME");
    log_dir_ = !home ? "/tmp/corecorder/log" : std::string(home) + "/.local/state/corecorder/log";
    create_directory(log_dir_);
  }

  ~Logger() {
    if (current_file_.is_open()) {
      current_file_.close();
    }
  }

  Logger(const Logger &) = delete;
  Logger& operator=(const Logger &) = delete;

  void log(LogLevel level, const std::string & message) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!should_log(level, current_level_)) {
      return;
    }

    check_and_rotate_log();

    if (current_file_.is_open()) {
      current_file_ << get_current_time_str() << " " <<
        "[" << get_level_string(level) << "] " <<
        message << std::endl;
      current_file_.flush();
    }
  }

  void set_log_dir(const std::string & dir) {
    if (log_dir_ == dir) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_file_.is_open()) {
      current_file_.close();
    }
    log_dir_ = dir;
    if (log_dir_.back() != '/') {
      log_dir_ += '/';
    }
    create_directory(log_dir_);
  }

  void set_log_level(const std::string & level) {
    if (current_level_string_ == level) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (level == "Debug" || level == "DEBUG" || level == "debug") {
      current_level_ = LogLevel::DEBUG;
    } else if (level == "Info" || level == "INFO" || level == "info") {
      current_level_ = LogLevel::INFO;
    } else if (level == "Warn" || level == "WARN" || level == "warn") {
      current_level_ = LogLevel::WARN;
    } else if (level == "Error" || level == "ERROR" || level == "error") {
      current_level_ = LogLevel::ERROR;
    }

    current_level_string_ = level;
  }

  void set_log_level(const LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_level_ = level;
  }

private:
  std::string log_dir_;
  std::mutex mutex_;
  std::ofstream current_file_;
  std::string current_date_;
  LogLevel current_level_;
  std::string current_level_string_;

  void check_and_rotate_log() {
    const std::string date = get_current_date_str();
    if (date != current_date_ || !current_file_.is_open()) {
      if (current_file_.is_open()) {
        current_file_.close();
      }

      current_date_ = date;
      const std::string filename = get_log_file_name(date);
      current_file_.open(filename, std::ios::app);

      clean_old_logs();
    }
  }

  void clean_old_logs() const {
    const int max_days = 7;
    DIR* dir = opendir(log_dir_.c_str());
    if (dir == nullptr) {
      return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (entry->d_type == DT_REG) {
        std::string filepath = log_dir_ + entry->d_name;
        struct stat file_stat{};
        if (stat(filepath.c_str(), &file_stat) == 0) {
          const auto now = std::time(nullptr);
          const double days = difftime(now, file_stat.st_mtime) / (60 * 60 * 24);

          if (days >= max_days) {
            remove(filepath.c_str());
          }
        }
      }
    }
    closedir(dir);
  }

  static std::string get_level_string(LogLevel level) {
    switch (level) {
      case LogLevel::DEBUG:
        return "DEBUG";
      case LogLevel::INFO:
        return " INFO";
      case LogLevel::WARN:
        return " WARN";
      case LogLevel::ERROR:
        return "ERROR";
      default:
        return "UNKNOWN";
    }
  }

  static std::string get_current_time_str() {
    timeval tv{};
    gettimeofday(&tv, nullptr);

    const time_t raw_time = tv.tv_sec;
    const tm* time_info = std::localtime(&raw_time);

    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", time_info);

    const int milliseconds = tv.tv_usec / 1000;

    std::ostringstream oss;
    oss << buffer << "." << std::setfill('0') << std::setw(3) << milliseconds;

    return oss.str();
  }

  static std::string get_current_date_str() {
    const std::time_t time = std::time(nullptr);
    const tm* time_info = std::localtime(&time);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", time_info);
    return std::string(buffer);
  }


  std::string get_log_file_name(const std::string & date) const {
    return log_dir_ + "coencoder_" + date + ".log";
  }

  static bool should_log(LogLevel msg_level, LogLevel filter_level) {
    return static_cast<int>(msg_level) >= static_cast<int>(filter_level);
  }
};

inline const char* get_filename(const char* path) {
  const char* filename = strrchr(path, '/');
  return filename ? filename + 1 : path;
}

inline std::string format_string(const char* file, int line, const char* msg) {
  return std::string("[") + get_filename(file) + ":" + std::to_string(line) + "] " + msg;
}

template <typename... Args>
std::string format_string(const char* file, int line, const char* format, Args... args) {
  int size = snprintf(nullptr, 0, format, args...) + 1;
  if (size <= 0) {
    return "Format Error";
  }
  std::vector<char> buf(size);
  snprintf(buf.data(), size, format, args...);
  return std::string("[") + get_filename(file) + ":" + std::to_string(line) + "] " +
    std::string(buf.data(), buf.data() + size - 1);
}

#define COLOG_INFO(...) \
  Logger::getInstance().log(LogLevel::INFO, format_string(__FILE__, __LINE__, __VA_ARGS__))

#define COLOG_WARN(...) \
  Logger::getInstance().log(LogLevel::WARN, format_string(__FILE__, __LINE__, __VA_ARGS__))

#define COLOG_ERROR(...) \
  Logger::getInstance().log(LogLevel::ERROR, format_string(__FILE__, __LINE__, __VA_ARGS__))

#define COLOG_DEBUG(...) \
  Logger::getInstance().log(LogLevel::DEBUG, format_string(__FILE__, __LINE__, __VA_ARGS__))

#endif  // UTILS__LOGGER_HPP_
