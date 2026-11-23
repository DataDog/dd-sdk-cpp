// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/http/body_writer_string.hpp"

#include "datadog/impl/core/http/body_writer_util.hpp"

namespace datadog::impl {

size_t StringWriter::operator()(char* buffer, size_t num_bytes) {
  // If an HTTP client implementation is silly enough to ask us to write to an empty
  // buffer, guard against it
  if (num_bytes == 0) {
    return 0;
  }

  return _handle_chunked_write(buffer, num_bytes, s.data(), s.size(), offset);
}

}  // namespace datadog::impl
