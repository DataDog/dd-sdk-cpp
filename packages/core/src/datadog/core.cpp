// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "core.h"

#include "datadog/internal/core_internal.h"
#include "datadog/internal/sdk_version.h"

namespace datadog::core {

using datadog::core::internal::DatadogCoreInternal;

std::shared_ptr<DatadogCore> DatadogCore::Create(
    const DatadogConfiguration& configuration) {
  return DatadogCoreInternal::Create(configuration);
}

}  // namespace datadog::core
