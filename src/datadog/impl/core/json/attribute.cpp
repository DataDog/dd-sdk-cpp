// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/json/attribute.hpp"

#include <charconv>
#include <cstring>

#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/json.hpp"

namespace datadog::impl {

/**
 * Returns the worst-case number of bytes required to represent an array of attributes
 * as a JSON array, accounting for brackets and delimiters, and recursively precomputing
 * the size of each value.
 */
static size_t _array_len(const Attribute& value) {
  // Early-out for empty array: '[]'
  const size_t num_items = value.GetArrayLen();
  if (num_items == 0) {
    return 2;
  }

  // 2 bytes for '[' and ']', plus N-1 commas to delimit the items
  const size_t brackets_len = 2;
  const size_t num_commas = num_items - 1;
  size_t len = brackets_len + num_commas;

  // Accumulate required space for all items, recursively
  for (int i = 0, num = static_cast<int>(num_items); i < num; i++) {
    len += GetJsonSize(value.GetArrayItem(i));
  }
  return len;
}

static size_t _array_write(char* dst, size_t n, const Attribute& value) {
  char* ptr = dst;
  char* const dst_end = dst + n;

  // Open array literal
  *ptr++ = '[';

  // Write each item, comma-delimited, serializing values recursively
  const size_t num_items = value.GetArrayLen();
  for (int i = 0, num = static_cast<int>(num_items); i < num; i++) {
    if (i > 0) {
      *ptr++ = ',';
    }
    ptr += WriteJson(ptr, dst_end - ptr, value.GetArrayItem(i));
  }

  // Close array literal
  *ptr++ = ']';

  // Return total number of bytes written, which should have been less than or equal to
  // the available space in the buffer
  const size_t num_bytes_written = ptr - dst;
  DATADOG_ASSERT(num_bytes_written <= n, "buffer overflow on array encode");
  return num_bytes_written;
}

/**
 * Returns the worst-case number of bytes required to represent an object attribute as a
 * JSON object, accounting for braces, delimiters, and literal property names, and
 * recursively precomputing the size of each property's value.
 */
static size_t _object_len(const Attribute& value) {
  // Early-out for empty object: '{}'
  const size_t num_properties = value.GetObjectPropertyCount();
  if (num_properties == 0) {
    return 2;
  }

  // 2 bytes for '{' and '}', plus N-1 commas to delimit the properties, plus N bytes
  // for the colon delimiting each property's name from its value
  const size_t braces_len = 2;
  const size_t num_commas = num_properties - 1;
  const size_t num_colons = num_properties;
  size_t len = braces_len + num_commas + num_colons;

  // Accumulate required space for each property name and value
  for (int i = 0, num = static_cast<int>(num_properties); i < num; i++) {
    len += GetJsonSize(value.GetObjectPropertyNameAt(i));
    len += GetJsonSize(value.GetObjectPropertyValueAt(i));
  }
  return len;
}

size_t GetJsonSize(const Attribute& value) {
  switch (value.GetType()) {
    case ValueType::Null:
      return GetJsonSize(nullptr);
    case ValueType::Bool:
      return GetJsonSize(value.GetBoolValue());
    case ValueType::Int:
      return GetJsonSize(value.GetIntValue());
    case ValueType::UInt:
      return GetJsonSize(value.GetUIntValue());
    case ValueType::Timestamp:
      return GetJsonSize(value.GetTimestampValue());
    case ValueType::Double:
      return GetJsonSize(value.GetDoubleValue());
    case ValueType::UUID:
      return GetJsonSize(value.GetUUIDValue());
    case ValueType::String:
      return GetJsonSize(value.GetStringValue());
    case ValueType::Array: {
      return _array_len(value);
    }
    case ValueType::Object: {
      return _object_len(value);
    }
  }
  DATADOG_ASSERT(false, "unhandled ValueType in GetJsonSize");
  return 0;
}

size_t WriteJson(char* dst, size_t n, const Attribute& value) {
  switch (value.GetType()) {
    case ValueType::Null:
      return WriteJson(dst, n, nullptr);
    case ValueType::Bool:
      return WriteJson(dst, n, value.GetBoolValue());
    case ValueType::Int:
      return WriteJson(dst, n, value.GetIntValue());
    case ValueType::UInt:
      return WriteJson(dst, n, value.GetUIntValue());
    case ValueType::Timestamp:
      return WriteJson(dst, n, value.GetTimestampValue());
    case ValueType::Double:
      return WriteJson(dst, n, value.GetDoubleValue());
    case ValueType::UUID:
      return WriteJson(dst, n, value.GetUUIDValue());
    case ValueType::String:
      return WriteJson(dst, n, value.GetStringValue());
    case ValueType::Array: {
      return _array_write(dst, n, value);
    }
    case ValueType::Object: {
      // Call our object-serialization helper function, specializing it to perform no
      // filtering based on property names
      return WriteFilteredJsonObject(dst, n, value, nullptr);
    }
  }
  DATADOG_ASSERT(false, "unhandled ValueType in WriteJson");
  return 0;
}

}  // namespace datadog::impl
