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
  MockDatadogCore(const CtorKey&) : context_(DatadogCoreConfiguration()) {};
  MockDatadogCore(const MockDatadogCore&) = delete;
  MockDatadogCore& operator=(const MockDatadogCore&) = delete;

  static std::shared_ptr<MockDatadogCore> Create() {
    return std::make_shared<MockDatadogCore>(CtorKey());
  }

  const DatadogCoreContext& GetCoreContext() const override { return context_; }

  void Write(FeatureId feature,
             std::function<void(const DatadogCoreContext& context,
                                datadog::core::internal::Writer*)>
                 write_callback) const override {}

  // Allow public modification of context as part of the mock
  DatadogCoreContext context_;

 private:
  explicit MockDatadogCore();
};

}  // namespace datadog::core::mocks
