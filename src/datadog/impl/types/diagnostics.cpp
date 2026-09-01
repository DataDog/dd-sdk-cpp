// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/diagnostics.hpp"

#include "datadog/impl/types/assert.hpp"
#include "datadog/impl/types/json.hpp"

namespace datadog::impl {

void DiagnosticLogger::Emit(
    DiagnosticLevel level, const char* text, DiagnosticAttributeList attributes
) const {
  if (handler && level >= threshold) {
    if (attributes.size() > 0) {
      // Determine how much space we need for '<message> <json-encoded-attributes>'
      std::string_view text_sv{text};
      const size_t json_size = GetJsonSize(attributes);
      const size_t buffer_size = text_sv.size() + 1 + json_size;

      // Allocate a string and append the message and delimiter to it
      std::string message;
      message.reserve(buffer_size);
      message += text_sv;
      message += ' ';

      // Grow to full size so we can write JSON-serialized attributes directly to buffer
      char* dst = message.data() + message.size();
      message.resize(buffer_size);
      char* const end = message.data() + message.size();
      DATADOG_ASSERT(
          static_cast<size_t>(end - dst) == json_size, "unexpected buffer size"
      );
      const size_t actual_json_size = WriteJson(dst, end - dst, attributes);
      DATADOG_ASSERT(actual_json_size <= json_size, "overflow on JSON write");

      // Shrink to fit
      message.resize(text_sv.size() + 1 + actual_json_size);

      // Emit the formatted message
      handler(DiagnosticMessage{level, message.c_str()});
    } else {
      // Emit the provided string constant as-is
      handler(DiagnosticMessage{level, text});
    }
  }
}

}  // namespace datadog::impl
