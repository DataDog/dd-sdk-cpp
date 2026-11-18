// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include "datadog/network_instrumentation.h"

#include "datadog/c/network_instrumentation_glue.hpp"

extern "C" {

size_t dd_http_headers_count(const dd_http_headers_t* headers) {
  return headers ? headers->headers.size() : 0;
}

bool dd_http_headers_get(
    const dd_http_headers_t* headers, size_t index, dd_http_header_t* out_header
) {
  if (!out_header) {
    return false;
  }

  *out_header = {};
  if (!headers || index >= headers->headers.size()) {
    return false;
  }

  const auto& header = headers->headers[index];
  out_header->name = header.GetName().data();
  out_header->value = header.GetValue().c_str();
  return true;
}

void dd_http_headers_destroy(dd_http_headers_t* headers) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete headers;
}
}
