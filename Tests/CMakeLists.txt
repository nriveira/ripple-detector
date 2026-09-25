cmake_minimum_required(VERSION 3.15)
project(ripple-detector-tests CXX)

# Standalone tests for the detection methods, the laser trigger request and the latency
# measurement. These do not need the Open Ephys GUI:
#
#   cmake -S Tests -B Tests/build && cmake --build Tests/build && ./Tests/build/detection_tests && ./Tests/build/laser_trigger_tests && ./Tests/build/latency_meter_tests

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB METHOD_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DetectionMethods/*.cpp)

add_executable(detection_tests DetectionMethodTests.cpp ${METHOD_SOURCES})
target_include_directories(detection_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)

enable_testing()
add_test(NAME detection_tests COMMAND detection_tests)

add_executable(laser_trigger_tests LaserTriggerHttpTests.cpp)
target_include_directories(laser_trigger_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
add_test(NAME laser_trigger_tests COMMAND laser_trigger_tests)

add_executable(latency_meter_tests StimLatencyMeterTests.cpp)
target_include_directories(latency_meter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
add_test(NAME latency_meter_tests COMMAND latency_meter_tests)
