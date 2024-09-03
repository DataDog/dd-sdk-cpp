// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include "datadog/core.h"
#include "datadog/time_provider.h"

namespace datadog::core::mocks {

using datadog::core::DatadogCoreConfiguration;
using datadog::core::IDatadogCore;

class MockDatadogCore : public IDatadogCore,
                        public std::enable_shared_from_this<MockDatadogCore> {
 public:
  explicit MockDatadogCore(Allow allow, const DatadogCoreConfiguration& config)
      : time_provider_(config.time_provider), context_(config) {};
  MockDatadogCore(const MockDatadogCore&) = delete;
  MockDatadogCore& operator=(const MockDatadogCore&) = delete;

  static std::shared_ptr<MockDatadogCore> Create(
      const DatadogCoreConfiguration& config) {
    return std::make_shared<MockDatadogCore>(Allow::ctor, config);
  }

  uint64_t GetNow() const noexcept override { return time_provider_(); }

  const DatadogCoreContext& GetCoreContext() const noexcept override {
    return context_;
  }

  void SendMesage(FeatureId feature, const CoreMessage& message) override {

  };

  // Allow public modification of members as part of the mock
  DateTimeProvider time_provider_;
  DatadogCoreContext context_;

 private:
  explicit MockDatadogCore();
};

}  // namespace datadog::core::mocks
