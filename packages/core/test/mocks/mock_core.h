// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include "datadog/core.h"

namespace datadog::core::mocks {

using datadog::core::IDatadogCore;

class MockDatadogCore : public IDatadogCore,
                        public std::enable_shared_from_this<MockDatadogCore> {
  struct CtorKey {
    explicit CtorKey() = default;
  };

 public:
  MockDatadogCore(const CtorKey&) {};
  MockDatadogCore(const MockDatadogCore&) = delete;
  MockDatadogCore& operator=(const MockDatadogCore&) = delete;

  static std::shared_ptr<MockDatadogCore> Create() {
    return std::make_shared<MockDatadogCore>(CtorKey());
  }

 private:
  explicit MockDatadogCore();
};

}  // namespace datadog::core::mocks
