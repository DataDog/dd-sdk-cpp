// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <string_view>

#include "datadog/reporting/reporter.h"

namespace datadog::core::reporting {

class LibcurlReporter : public DatadogReporter {
 public:
  explicit LibcurlReporter(std::string_view host) : host_{host} {}
  ~LibcurlReporter() = default;

  Status Send(const Report&) override;

  static std::unique_ptr<DatadogReporter> Create(std::string_view host);

 private:
  std::string host_;
};

}  // namespace datadog::core::reporting
