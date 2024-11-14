project(datadog-sdk-core)

find_package(Threads REQUIRED)

add_library(datadog-core STATIC
  src/datadog/core.cpp
  
  src/datadog/internal/core_internal.cpp
  src/datadog/internal/performance_preset.cpp
  src/datadog/time_provider.cpp

  src/datadog/storage/datadog_file_system.cpp
  src/datadog/storage/feature_storage.cpp
  src/datadog/storage/tlv_file_reader.cpp
)
target_include_directories(datadog-core PRIVATE
  .
  src/
)
add_compile_options(-frtti)
target_link_libraries(datadog-core PRIVATE 
  common_options
  ${CMAKE_THREAD_LIBS_INIT}
)

if(DD_SDK_BUILD_TEST)
  add_subdirectory(test)
endif()
