project(datadog-sdk-logging)

add_library(datadog-logging STATIC
  src/datadog/logging_feature.cpp
  src/datadog/logger.cpp
)
target_include_directories(datadog-logging PRIVATE
  ../core/src/
  .
  src/
  ${CMAKE_SOURCE_DIR}/deps/date/include
)
target_link_libraries(datadog-logging PRIVATE common_options)

if(DD_SDK_BUILD_TEST)
  add_subdirectory(test)
endif()
