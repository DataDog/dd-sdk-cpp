project(datadog-logging)

set(DD_CORE_DIR "../core")

add_library(datadog-logging STATIC
  src/datadog/logging.cpp
  src/datadog/logger.cpp
)
target_include_directories(datadog-logging PRIVATE
  src/
  ${DD_CORE_DIR}/src
)

if(DD_SDK_BUILD_TEST)
  add_subdirectory(test)
endif()
