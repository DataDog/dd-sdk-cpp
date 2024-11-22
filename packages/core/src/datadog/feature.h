// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.

#pragma once

#include <string>

#include "datadog/internal/core_context.h"
#include "datadog/reporting/report.h"
#include "datadog/storage/tlv_file_reader.h"

namespace datadog::core {

enum class FeatureId : uint32_t {};

class DatadogFeature {
 public:
  virtual ~DatadogFeature() = default;

  virtual std::string_view GetName() const = 0;

  virtual reporting::Report CreateReportFromBatch(
      const datadog::core::internal::CoreContext& core_context,
      storage::TLVFileReader& file_reader) const = 0;
};

}  // namespace datadog::core
