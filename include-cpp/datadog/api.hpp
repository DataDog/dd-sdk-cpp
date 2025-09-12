#pragma once

#ifdef _WIN32
#ifdef DATADOG_SHARED_LIB
#ifdef WITH_DATADOG_EXPORTS
#define DATADOG_API __declspec(dllexport)
#else
#define DATADOG_API __declspec(dllimport)
#endif
#else
#define DATADOG_API
#endif
#else
#ifdef DATADOG_SHARED_LIB
#define DATADOG_API __attribute__((visibility("default")))
#else
#define DATADOG_API
#endif
#endif
