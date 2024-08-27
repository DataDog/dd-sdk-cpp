// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <datadog/core.h>
#include <datadog/feature.h>
#include <datadog/internal/utils.h>

#include "datadog/logger.h"

namespace datadog::logging {

using datadog::core::DatadogFeature;
using datadog::core::FeatureId;
using datadog::core::IDatadogCore;

class LoggingFeature : public DatadogFeature {
 public:
  explicit LoggingFeature(const std::shared_ptr<IDatadogCore>& core);

  std::unique_ptr<Logger> CreateLogger(const LoggerOptions& options);

  static constexpr const FeatureId kFeatureId =
      datadog::core::internal::four_cc('L', 'O', 'G', 'S');

 private:
  LoggingFeature() = delete;

  std::weak_ptr<IDatadogCore> core_;
};

}  // namespace datadog::logging
