// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/reporting/reporter.h"

namespace datadog::core::reporting {

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
DatadogReporter::Status DatadogReporter::FromHttpStatusCode(int status_code) {
  switch (status_code) {
    case 200:  // Datadog Intake doesn't return this but mock servers do.
    case 202:  // Accepted
      return DatadogReporter::Status::Ok;

    case 408:  // Request Timeout
    case 429:  // Too Many Requests
    case 500:  // Internal Server Error
    case 502:  // Bad Gateway
    case 503:  // Service Unavailable
    case 504:  // Gateway timeout
    case 507:  // Insufficient Stroage
      return DatadogReporter::Status::ErrorNeedsRetry;

    case 400:  // Bad Request
    case 401:  // Unauthorized
    case 403:  // Forbidden
    case 413:  // Payload Too Large
    default:
      return DatadogReporter::Status::UnrecoverableError;
  }
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

}  // namespace datadog::core::reporting
