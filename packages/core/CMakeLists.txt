project(datadog-sdk-core)

add_library(datadog-core STATIC
  src/datadog/core.cpp
  src/datadog/time_provider.cpp
)
target_include_directories(datadog-core PRIVATE
  src/
)
add_compile_options(-frtti)
if(DD_SDK_BUILD_TEST)
  add_subdirectory(test)
endif()
