project(datadog-sdk-core)

add_library(datadog-core STATIC
  src/datadog/core.cpp
)
target_include_directories(datadog-core PRIVATE
  src/
)

if(DD_SDK_BUILD_TEST)
  add_subdirectory(test)
endif()
