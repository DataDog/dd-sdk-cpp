// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/diagnostic_attribute.hpp"

#include <charconv>
#include <cstring>

#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/json.hpp"

namespace datadog::impl {

size_t GetJsonSize(const DiagnosticAttributeValue& value) {
  return std::visit([](const auto& v) { return GetJsonSize(v); }, value);
}

size_t WriteJson(char* dst, size_t n, const DiagnosticAttributeValue& value) {
  return std::visit([dst, n](const auto& v) { return WriteJson(dst, n, v); }, value);
}

size_t GetJsonSize(const DiagnosticAttributeList& value) {
  size_t size = 2;  // {}
  const size_t n = value.size();
  if (n > 0) {
    size += n - 1;  // commas, without space
    size += n;      // colons, without space
    for (const auto& kvp : value) {
      size += kvp.key.size() + 2;      // quoted name, no escaping
      size += GetJsonSize(kvp.value);  // JSON-serialized value
    }
  }
  return size;
}

size_t WriteJson(char* dst, size_t n, const DiagnosticAttributeList& value) {
  char* ptr = dst;
  char* end = dst + n;

  // Wrote open bracket
  DATADOG_ASSERT(ptr < end, "JSON open bracket would overflow buffer");
  *ptr++ = '{';

  // Write all attributes, JSON-serializing their values
  bool is_first = true;
  for (const auto& kvp : value) {
    // Write ',' before every value but the first
    if (!is_first) {
      DATADOG_ASSERT(ptr < end, "JSON comma would overflow buffer");
      *ptr++ = ',';
    }
    is_first = false;

    // Write '"<name>":', without escaping the attribute name
    DATADOG_ASSERT(
        ptr + kvp.key.size() + 3 < end, "JSON property name would overflow buffer"
    );
    *ptr++ = '"';
    std::memcpy(ptr, kvp.key.data(), kvp.key.size());
    ptr += kvp.key.size();
    *ptr++ = '"';
    *ptr++ = ':';

    // Write the Attribute value as JSON
    const size_t json_value_size = WriteJson(ptr, end - ptr, kvp.value);
    DATADOG_ASSERT(ptr + json_value_size < end, "JSON value has overflowed buffer");
    ptr += json_value_size;
  }

  // Write close bracket
  DATADOG_ASSERT(ptr < end, "JSON close bracket would overflow buffer");
  *ptr++ = '}';

  // Return total bytes written
  return ptr - dst;
}

}  // namespace datadog::impl
