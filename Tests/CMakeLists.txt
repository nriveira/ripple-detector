cmake_minimum_required(VERSION 3.15)
project(ripple-detector-tests CXX)

# Standalone tests for the detection methods. These do not need the Open Ephys GUI:
#
#   cmake -S Tests -B Tests/build && cmake --build Tests/build && ./Tests/build/detection_tests

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
