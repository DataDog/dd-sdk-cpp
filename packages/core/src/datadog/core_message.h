// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <string>

#include "datadog/attribute.h"
#include "datadog/feature.h"

namespace datadog::core {

class CoreMessage {
 public:
  explicit CoreMessage(FeatureId feature_id,
                       const DatadogAttribute& context_changes,
                       std::string&& data)
      : feature_id_{feature_id},
        context_changes_(context_changes),
        data_(std::move(data)) {}

  FeatureId feature_id() const { return feature_id_; }
  const DatadogAttribute& context_changes() const { return context_changes_; }
  const std::string& data() const { return data_; }

 private:
  FeatureId feature_id_;
  DatadogAttribute context_changes_;
  std::string data_;
};

}  // namespace datadog::core
