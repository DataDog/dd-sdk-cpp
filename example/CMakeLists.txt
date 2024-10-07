cmake_minimum_required(VERSION 3.21)

project(dd-sdk-cpp LANGUAGES CXX)

add_executable(datadog-example
  main.cpp
)

target_include_directories(datadog-example PRIVATE
  ../packages/core/src
  ../packages/logging/src
)
target_link_libraries(datadog-example PRIVATE
  common_options
  datadog-core
  datadog-logging
)
