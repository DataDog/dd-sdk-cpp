project(datadog-sdk-core)

find_package(Threads REQUIRED)

option(DD_SDK_INCLUDE_LIBCURL_REPORTER "Include the default reporter that relies on libcurl" ON)

set(DD_SDK_CORE_SOURCES
  src/datadog/core.cpp  
  src/datadog/time_provider.cpp
  src/datadog/attribute.cpp

  src/datadog/internal/core_internal.cpp
  src/datadog/internal/performance_preset.cpp
  src/datadog/internal/reporting_thread.cpp

  src/datadog/reporting/reporter.cpp

  src/datadog/storage/datadog_file_system.cpp
  src/datadog/storage/feature_storage.cpp
  src/datadog/storage/tlv_file_reader.cpp
)

if(DD_SDK_INCLUDE_LIBCURL_REPORTER)
  list(APPEND DD_SDK_CORE_SOURCES
    src/datadog/reporting/libcurl_reporter.cpp
  )
endif()

add_library(datadog-core STATIC ${DD_SDK_CORE_SOURCES})

if(DD_SDK_INCLUDE_LIBCURL_REPORTER)
  include(../../cmake/deps/libcurl.cmake)

  add_dependencies(datadog-core CURL::libcurl_shared)
  set(LIBCURL_SHARED CURL::libcurl_shared)
endif()

target_include_directories(datadog-core PRIVATE
  .
  src/
)
add_compile_options(-frtti)
target_link_libraries(datadog-core PRIVATE 
  common_options
  ${CMAKE_THREAD_LIBS_INIT}
  ${LIBCURL_SHARED}
)

if(DD_SDK_BUILD_TEST)
  add_subdirectory(test)
endif()
