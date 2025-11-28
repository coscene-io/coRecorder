cmake_minimum_required(VERSION 3.10)
project(corecorder)

# Set policy to allow older googletest versions
if (POLICY CMP0148)
    cmake_policy(SET CMP0148 OLD)
endif ()

# Default to C99
if (NOT CMAKE_C_STANDARD)
    set(CMAKE_C_STANDARD 99)
endif ()

# Default to C++14
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif ()

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

set(PRIVATE_MCAP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/mcap/1.4.2/include)
include_directories(BEFORE ${PRIVATE_MCAP_DIR})

# Find compression libraries for MCAP
find_package(PkgConfig REQUIRED)
pkg_check_modules(LZ4 REQUIRED liblz4)
pkg_check_modules(ZSTD REQUIRED libzstd)

if ("$ENV{ROS_VERSION}" STREQUAL "1")
    message(STATUS "Building with catkin")
    set(ROS_BUILD_TYPE "catkin")
    add_definitions(-DROS_VERSION_1)

    # Disable gtest from source to avoid CMake version conflicts
    set(CATKIN_ENABLE_TESTING OFF)

    # Find catkin first to get catkin_INCLUDE_DIRS
    find_package(catkin REQUIRED COMPONENTS roscpp message_generation roslib topic_tools)
    add_service_files(
            FILES
            RecordingControl.srv
            RecordingStatus.srv
            CacheRecordingConfig.srv
    )
    generate_messages(DEPENDENCIES std_msgs)
    catkin_package(
            CATKIN_DEPENDS roscpp message_runtime roslib topic_tools
    )

    include_directories(${catkin_INCLUDE_DIRS}
            ${PROJECT_SOURCE_DIR}/include/ros1
            ${PROJECT_SOURCE_DIR}/include/common
    )

    add_executable(corecorder_node src/ros1/main.cpp)
    add_dependencies(corecorder_node ${PROJECT_NAME}_generate_messages_cpp)
    target_link_libraries(corecorder_node
            ${catkin_LIBRARIES}
            ${LZ4_LIBRARIES}
            ${ZSTD_LIBRARIES}
    )

    target_include_directories(corecorder_node PRIVATE
            ${LZ4_INCLUDE_DIRS}
            ${ZSTD_INCLUDE_DIRS}
    )
elseif ("$ENV{ROS_VERSION}" STREQUAL "2")
    message(STATUS "Building with ament_cmake")
    add_definitions(-DROS_VERSION_2)
    if ("$ENV{ROS_DISTRO}" STREQUAL "foxy")
        add_definitions(-DROS2_VERSION_FOXY)
    endif ()

    set(ROS_BUILD_TYPE "ament_cmake")

    find_package(ament_cmake REQUIRED)
    find_package(rclcpp REQUIRED)
    find_package(ament_index_cpp REQUIRED)
    find_package(rosidl_default_generators REQUIRED)

    # Generate interfaces
    rosidl_generate_interfaces(${PROJECT_NAME}
            "srv/RecordingControl.srv"
            "srv/RecordingStatus.srv"
            "srv/CacheRecordingConfig.srv"
    )

    add_executable(corecorder_node
            src/ros2/main.cpp
    )

    target_include_directories(corecorder_node PRIVATE
            ${PROJECT_SOURCE_DIR}/include/ros2
            ${PROJECT_SOURCE_DIR}/include/common
            $<INSTALL_INTERFACE:include>
    )

    ament_target_dependencies(corecorder_node rclcpp ament_index_cpp)

    target_link_libraries(corecorder_node
            ${LZ4_LIBRARIES}
            ${ZSTD_LIBRARIES}
    )

    target_include_directories(corecorder_node PRIVATE
            ${LZ4_INCLUDE_DIRS}
            ${ZSTD_INCLUDE_DIRS}
    )

    # Link generated interfaces to the executable
    rosidl_target_interfaces(corecorder_node ${PROJECT_NAME} "rosidl_typesupport_cpp")

    # Export dependencies for other packages that depend on this one
    ament_export_dependencies(rosidl_default_runtime)

    if (BUILD_TESTING)
        find_package(ament_lint_auto REQUIRED)
        ament_lint_auto_find_test_dependencies()
    endif ()
else ()
    message(FATAL_ERROR "ROS_VERSION environment variable must be 1 or 2")
endif ()


if (ROS_BUILD_TYPE STREQUAL "catkin")
    install(TARGETS corecorder_node
            RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
    )

    #  install(DIRECTORY src/ros1/launch
    #          DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/launch
    #          FILES_MATCHING PATTERN "*.launch"
    #  )
elseif (ROS_BUILD_TYPE STREQUAL "ament_cmake")
    install(TARGETS corecorder_node
            DESTINATION lib/${PROJECT_NAME}
    )

    #  install(DIRECTORY src/ros2/launch
    #          DESTINATION share/${PROJECT_NAME}/
    #  )
    ament_package()
endif ()
