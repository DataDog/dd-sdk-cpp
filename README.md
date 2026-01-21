# Datadog C++ SDK

## Overview

The Datadog C++ SDK provides client-side monitoring functionality for your C or C++ applications.

By integrating the SDK into your application and using features like Logging and [Real User Monitoring (RUM)](https://docs.datadoghq.com/real_user_monitoring/), you can visualize and analyze the real-time performance and user journeys of your application's individual users.

In addition to adding SDK functionality to native applications, the Datadog C++ SDK also makes it possible to create Foreign Function Interface (FFI) bindings to other languages [through its C API](#using-the-c-api-1).

## Building and Installation

To add the Datadog C++ SDK into your existing CMake project, you can use `FetchContent` to direct CMake to this repository, using the commit hash of your chosen release:

```cmake
include(FetchContent)
FetchContent_Declare(
    Datadog
    GIT_REPOSITORY https://github.com/DataDog/dd-sdk-cpp.git
    GIT_TAG        0000000000000000000000000000000000000000  # v0.0.0
)
FetchContent_MakeAvailable(Datadog)
```

This pulls the SDK into your CMake build tree and builds it from source. In your own CMake configuration, you can configure the SDK's build using any of the options defined in [`CMakeLists.txt`](./CMakeLists.txt).

For example, to build as a static library, with `libcurl` built from source and linked into the `dd-sdk-cpp` library:

```cmake
set(DD_BUILD_SHARED OFF)
set(DD_HTTP_USE_SYSTEM_LIBCURL OFF)
```

To use the SDK within your own projects, add `Datadog::dd_native` as a dependency for the relevant targets:

```
target_link_libraries(your_library_or_program Datadog::dd_native)
```

## Using the C++ API

The Datadog C++ SDK provides an idiomatic C++ API which is suitable for use in any codebase targeting C++17 or newer.

You can peruse the public C++ headers [here](./include-cpp/datadog/).

```cpp
#include "datadog.hpp"

void your_application_code() {
  // Initialize SDK configuration, specifying the path to a directory where your
  // application can store transient files: the SDK will create .datadog/ there
  auto config = datadog::CoreConfig("<your-client-id>", "<your-service>", "<your-env>");
  config.SetEventStorageLocation("<your-storage-directory>");

  // Create the core, set tracking consent based on user input
  auto core = datadog::Core::Create(config);
  core->SetTrackingConsent(datadog::TrackingConsent::Granted);

  // Register features and call feature-specific APIs
  auto logging = datadog::Logging::Register(core);
  auto logger = logging->CreateLogger();

  // Start the SDK, use features while running, and stop when done
  core->Start();
  logger->Info("This message will be sent to Datadog");
  core->Stop();
}
```

For more detailed usage examples, see [the C++ example program](./examples/cpp/main.cpp).

## Using the C API

The SDK also provides a C99-compliant API for use in C codebases, and for interoperability with languages and runtimes that expect an FFI with C linkage.

You can peruse the public C headers [here](./include-c/datadog/).

```c
#include "datadog.h"

void your_application_code() {
  // Initialize SDK configuration, specifying the path to a directory where your
  // application can store transient files: the SDK will create .datadog/ there
  dd_core_config_t config;
  dd_core_config_init(&config, "<your-client-id>", "<your-service>", "<your-env>");
  dd_core_config_set_event_storage_location(&config, "<your-storage-directory>");

  // Create the core, set tracking consent based on user input
  dd_core_t* core = dd_core_create(&config);
  dd_core_set_tracking_consent(core, DD_TRACKING_CONSENT_GRANTED);

  // Register features and call feature-specific APIs
  dd_logging_t* logging = dd_logging_init(core);
  dd_logger_t* logger = dd_logger_create(logging, NULL);

  // Start the SDK, use features while running, and stop when done
  dd_core_start(&core);
  dd_logger_info(logger, "This message will be sent to Datadog");
  dd_core_stop(&core);

  // Clean up SDK resources when done
  dd_logger_destroy(logger);
  dd_logging_destroy(logging);
  dd_core_destroy(core);
}
```

For more detailed usage examples, see [the C example program](./examples/c/main.c).

## Contributing

For more information about working on the SDK itself, see [`CONTRIBUTING.md`](./CONTRIBUTING.md).

## License

The Datadog C++ SDK is licensed under the [Apache License Version 2.0](./LICENSE.md).
