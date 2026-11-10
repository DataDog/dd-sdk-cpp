// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

namespace datadog::impl {

class B3HeaderWriter {
 public:
  enum class Encoding { Multiple, Single };

  B3HeaderWriter() = delete;

  static void AppendHeaders(
      TraceContext& context, Encoding encoding, std::vector<HttpHeader>& out
  );
};

}  // namespace datadog::impl
