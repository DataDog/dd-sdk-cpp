// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <memory>
#include <string_view>

#include "datadog/reporting/report.h"

namespace datadog::core::reporting {

// Interface for reporting data to Datadog.  The default implementation is
// LibCurlReporter, which relies on libcurl to send data.
class DatadogReporter {
 public:
  enum class Status {
    // The batch was received with no issues. This informs the Core that the
    // batch can safely be deleted.
    Ok,

    // The batch failed to be sent, but the error is temporary. The Core should
    // attempt to resend this batch some time in the future. This includes
    // situations where the network is temporarily unavailable, or if Datadog
    // intake returns the following status codes:
    //  - 408 Request Timeout
    //  - 429 Too Many Requests
    //  - 500 Internal Server ERror
    //  - 502 Bad Gateway
    //  - 503 Service Unavailable
    //  - 504 Gateway timeout
    //  - 507 Insufficient Storage
    ErrorNeedsRetry,

    // The batch failed to be sent and the error is not temporary. This should
    // be returned if Datadog intake returns the following status codes:
    //  - 400 Bad Request
    //  - 401 Unauthorized
    //  - 403 Forbidden
    //  - 413 Payload Too Large
    UnrecoverableError,
  };
  using CreateFunc =
      std::unique_ptr<DatadogReporter> (*)(std::string_view host);

  // Translate an HTTP status code recieved from Datadog Intake to a
  // DatadogReporter::Status
  static Status FromHttpStatusCode(int status_code);

  virtual ~DatadogReporter() = default;

  // Report data to Datadog at the specified path, using the provided headers
  // and body. The path will not contain the host URL.
  virtual Status Send(const Report& report) = 0;
};

}  // namespace datadog::core::reporting
