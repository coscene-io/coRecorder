// Copyright (c) 2019 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "ros_babel_fish/generation/providers/integrated_description_provider.h"

#include <ros/package.h>

// experimental/filesystem
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fstream>
#include <regex>

namespace ros_babel_fish
{

namespace
{
#ifdef _WIN32
const std::string OS_PATHSEP(";");
#else
const std::string OS_PATHSEP( ":" );
#endif

const std::string CATKIN_MARKER_FILE = ".catkin";

bool file_exists(const std::string& path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0);
}

bool is_regular_file(const std::string& path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0) && S_ISREG(buffer.st_mode);
}

bool is_directory(const std::string& path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0) && S_ISDIR(buffer.st_mode);
}

std::string join_path(const std::string& base, const std::string& append) {
  if (base.empty()) return append;
  if (append.empty()) return base;

  std::string result = base;
  if (result.back() != '/') {
    result += '/';
  }
  result += append;
  return result;
}

std::vector<std::string> findWorkspaceShares()
{
  char *cmake_prefix_path = std::getenv( "CMAKE_PREFIX_PATH" );
  if ( cmake_prefix_path == nullptr ) return {};
  size_t length = std::strlen( cmake_prefix_path );
  std::vector<std::string> paths;
  std::string path;
  size_t path_sep_index = 0;
  for ( size_t i = 0; i <= length; ++i )
  {
    if ( i == length || (cmake_prefix_path[i] == OS_PATHSEP[path_sep_index] && ++path_sep_index == OS_PATHSEP.length()))
    {
      if ( !path.empty())
      {
        std::string catkin_marker_path = join_path(path, CATKIN_MARKER_FILE);
        std::string share_path = join_path(path, "share");
        if ( is_regular_file( catkin_marker_path ) && is_directory( share_path ))
        {
          paths.push_back( share_path );
        }
      }
      path_sep_index = 0;
      path.clear();
      continue;
    }
    path_sep_index = 0;
    path.push_back( cmake_prefix_path[i] );
  }

  return paths;
}
}

IntegratedDescriptionProvider::IntegratedDescriptionProvider()
{
  ros::V_string packages;
  if ( !ros::package::getAll( packages ))
    throw BabelFishException( "Failed to retrieve package paths, will not be able to look up message definitions!" );

  std::vector<std::string> workspace_shares = findWorkspaceShares();
  for ( auto &pkg : packages )
  {
    std::string base_path = ros::package::getPath( pkg );
    std::vector<std::string> msg_paths, srv_paths;

    // First check directories returned by ros::package::getPath
    std::string msg_path = join_path(base_path, "msg");
    if ( is_directory( msg_path ))
      msg_paths.push_back( msg_path );

    std::string srv_path = join_path(base_path, "srv");
    if ( is_directory( srv_path ))
      srv_paths.push_back( srv_path );

    // Then check first result in workspaces
    for ( const std::string &workspace_share : workspace_shares )
    {
      std::string project_path = join_path(workspace_share, pkg);
      if ( is_directory( project_path ))
      {
        if ( project_path == base_path ) break;
        std::string workspace_msg_path = join_path(project_path, "msg");
        if ( is_directory( workspace_msg_path ))
          msg_paths.push_back( workspace_msg_path );
        std::string workspace_srv_path = join_path(project_path, "srv");
        if ( is_directory( workspace_srv_path ))
          srv_paths.push_back( workspace_srv_path );
        // Only add the first match and only if it differed from the base_path
        break;
      }
    }
    if ( !msg_paths.empty()) msg_paths_.insert( { pkg, msg_paths } );
    if ( !srv_paths.empty()) srv_paths_.insert( { pkg, srv_paths } );
  }
}

MessageDescription::ConstPtr IntegratedDescriptionProvider::getMessageDescriptionImpl( const std::string &type )
{
  if ( type == "Header" ) return getMessageDescription( "std_msgs/Header" );
  std::string::size_type pos_separator = type.find( '/' );
  if ( pos_separator == std::string::npos )
  {
    ROS_WARN_NAMED( "RosBabelFish", "Type '%s' is not a valid message type!", type.c_str());
    return nullptr;
  }
  std::string package = type.substr( 0, pos_separator );
  std::string msg_type = type.substr( package.length() + 1 );
  auto it = msg_paths_.find( package );
  if ( it == msg_paths_.end())
  {
    ROS_WARN_NAMED( "RosBabelFish", "Could not find package '%s' in msg paths!", package.c_str());
    return nullptr;
  }
  const std::vector<std::string> &package_paths = it->second;
  std::string message_path;
  for ( const auto &package_path : package_paths )
  {
    std::string p = join_path(package_path, msg_type + ".msg");
    if ( !is_regular_file( p )) continue;
    message_path = p;
    break;
  }
  if ( message_path.empty())
  {
    ROS_WARN_NAMED( "RosBabelFish", "Could not find message of type '%s' in package '%s'!", msg_type.c_str(),
                    package.c_str());
    return nullptr;
  }

  // Load message specification from file
  std::ifstream file_input( message_path.c_str() );
  file_input.seekg( 0, std::ios::end );
  std::string specification;
  specification.resize( file_input.tellg());
  file_input.seekg( 0, std::ios::beg );
  file_input.read( &specification[0], specification.size());
  file_input.close();

  return registerMessage( type, specification );
}

ServiceDescription::ConstPtr IntegratedDescriptionProvider::getServiceDescriptionImpl( const std::string &type )
{
  std::string::size_type pos_separator = type.find( '/' );
  if ( pos_separator == std::string::npos )
  {
    ROS_WARN_NAMED( "RosBabelFish", "Type '%s' is not a valid service type!", type.c_str());
    return nullptr;
  }
  std::string package = type.substr( 0, pos_separator );
  std::string msg_type = type.substr( package.length() + 1 );
  auto it = srv_paths_.find( package );
  if ( it == srv_paths_.end())
  {
    ROS_WARN_NAMED( "RosBabelFish", "Could not find package '%s' in srv paths!", package.c_str());
    return nullptr;
  }
  const std::vector<std::string> &package_paths = it->second;
  std::string service_path;
  for ( const auto &package_path : package_paths )
  {
    std::string p = join_path(package_path, msg_type + ".srv");
    if ( !is_regular_file( p )) continue;
    service_path = p;
    break;
  }
  if ( service_path.empty())
  {
    ROS_WARN_NAMED( "RosBabelFish", "Could not find service of type '%s' in package '%s'!", msg_type.c_str(),
                    package.c_str());
    return nullptr;
  }

  // Load service specification from file
  std::ifstream file_input( service_path.c_str() );
  file_input.seekg( 0, std::ios::end );
  std::string spec;
  spec.resize( file_input.tellg());
  file_input.seekg( 0, std::ios::beg );
  file_input.read( &spec[0], spec.size());
  file_input.close();
  if ( spec.length() < 3 )
  {
    ROS_ERROR_NAMED( "RosBabelFish", "Service specification for type '%s' in package '%s' was invalid!",
                     msg_type.c_str(), package.c_str());
    return nullptr;
  }

  // Extract service request and response specifications which are divided by '---' on a blank line
  std::string::size_type end = 0;
  if ( spec[0] != '-' || spec[1] != '-' || spec[2] != '-' )
    end = spec.find( "\n---" );
  std::string request( spec, 0, end );

  std::string response;
  if ( end != std::string::npos )
  {
    end = spec.find( '\n', end + 1 );
    if ( end != std::string::npos && end + 1 < spec.length())
      response = std::string( spec, end + 1 ) + "\n";
  }

  return registerService( type, spec, request, response );
}
} // ros_babel_fish
