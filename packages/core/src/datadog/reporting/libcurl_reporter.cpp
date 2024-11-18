// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/reporting/libcurl_reporter.h"

namespace datadog::core::reporting {

std::unique_ptr<DatadogReporter> LibcurlReporter::Create(
    [[maybe_unused]] std::string_view host) {
  return std::make_unique<LibcurlReporter>(host);
}

DatadogReporter::Status LibcurlReporter::Send(const Report&) {
  return DatadogReporter::Status::UnrecoverableError;
}

}  // namespace datadog::core::reporting
