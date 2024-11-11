// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <trompeloeil.hpp>

#include "datadog/reporting/reporter.h"

namespace datadog::core::reporting::mocks {

class MockDatadogReporter : public DatadogReporter {
 public:
  MockDatadogReporter(std::string_view host) : host(host) {}

  MAKE_MOCK1(Send, Status(const Report&), override);

  std::string host;
};

}  // namespace datadog::core::reporting::mocks
