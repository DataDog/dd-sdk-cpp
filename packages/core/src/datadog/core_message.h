// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include "attribues.h"

namespace datadog::core {
class CoreMessage {
 public:
  explicit CoreMessage(const DatadogAttributes& context_changes,
                       const std::string& data)
      : context_changes_(context_changes), data_(data) {}

  const DatadogAttributes& GetContextChanges() const noexcept {
    return context_changes_;
  }
  const std::string& GetData() const noexcept { return data_; }

 private:
  DatadogAttributes context_changes_;
  std::string data_;
};

}  // namespace datadog::core
