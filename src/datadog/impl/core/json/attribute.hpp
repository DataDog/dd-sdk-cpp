// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <optional>

#include "datadog/attribute.hpp"

#include "datadog/impl/core/json/primitives/string.hpp"
#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

/**
 * Result returned by TryEncodeJson for Attribute values.
 *
 * `bytes_written` is the number of bytes written to the output buffer.
 * `truncated` is true if one or more top-level object properties were dropped
 * because they did not fit within the buffer.
 */
struct AttributeEncodeResult {
  size_t bytes_written;
  bool truncated;
};

/**
 * Returns the number of bytes required to encode the given Attribute value in JSON
 * format.
 */
size_t GetJsonSize(const Attribute& value);

/**
 * Encodes the given Attribute value in JSON format.
 */
size_t WriteJson(char* dst, size_t n, const Attribute& value);

/**
 * Attempts to serialize the given Attribute value into the fixed-size buffer `dst`
 * (capacity `n` bytes), returning an AttributeEncodeResult on success, or std::nullopt
 * if the value cannot fit even in its most-reduced form.
 *
 * For non-object types and empty objects, the value is written as-is if it fits;
 * otherwise std::nullopt is returned. `truncated` is always false in these cases.
 *
 * For object types with one or more properties, writes the largest prefix of top-level
 * properties (in order) that fits within `n` bytes. If all properties fit, `truncated`
 * is false. If one or more properties had to be dropped from the back, `truncated` is
 * true. If no individual property fits alongside the enclosing braces (i.e. the result
 * is `{}`), `truncated` is true. Returns std::nullopt only if `n < 2`.
 */
std::optional<AttributeEncodeResult> TryEncodeJson(
    char* dst, size_t n, const Attribute& value
);

/**
 * Special-case function for use in JSON struct serialization routines: encodes the
 * given Attribute value as a JSON object, excluding any top-level properties for which
 * `filter_func(name)` returns false.
 *
 * @param filter_func - A callable that takes a property name and returns true if
 *  it should be included in the serialized JSON object, or false if that name should be
 *  excluded due to a conflict with a standard property name.
 */
template <typename FilterFunc>
size_t WriteFilteredJsonObject(
    char* dst, size_t n, const Attribute& value, FilterFunc filter_func
) {
  char* ptr = dst;
  char* const dst_end = dst + n;

  // Open object literal
  *ptr++ = '{';

  // Write name:value pairs for each property, comma-delimited, serializing values
  // recursively
  size_t num_properties_written = 0;
  const size_t num_properties = value.GetObjectPropertyCount();
  for (int i = 0, num = static_cast<int>(num_properties); i < num; i++) {
    // If we've been given a filter func, ensure that any properties with reserved names
    // (i.e. those where filter_func(name) => false) are skipped
    std::string_view property_name = value.GetObjectPropertyNameAt(i);
    if constexpr (!std::is_same_v<FilterFunc, std::nullptr_t>) {
      if (!filter_func(property_name)) {
        continue;
      }
    }

    // Prepend a comma for all values except the first
    if (num_properties_written > 0) {
      *ptr++ = ',';
    }

    // Write property name and value, colon-delimited
    ptr += WriteJson(ptr, dst_end - ptr, property_name);
    *ptr++ = ':';
    ptr += WriteJson(ptr, dst_end - ptr, value.GetObjectPropertyValueAt(i));
    num_properties_written++;
  }

  // Close object literal
  *ptr++ = '}';

  // Return total number of bytes written, which should have been less than or equal to
  // the available space in the buffer
  const size_t num_bytes_written = ptr - dst;
  DATADOG_ASSERT(num_bytes_written <= n, "buffer overflow on object encode");
  return num_bytes_written;
}

}  // namespace datadog::impl
