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

std::optional<AttributeEncodeResult> TryEncodeJson(
    char* dst, size_t n, const Attribute& value
) {
  // If this value is an empty object or has a type other than object, serialize it
  // as-is if it fits, without attempting any kind of truncation
  const size_t num_object_properties = value.GetObjectPropertyCount();
  if (num_object_properties <= 0) {
    const size_t num_bytes_required = GetJsonSize(value);
    if (num_bytes_required > n) {
      return std::nullopt;
    }
    return AttributeEncodeResult{WriteJson(dst, n, value), false};
  }

  // We now know that we're dealing with a non-empty object value: verify that we can
  // fit at least an empty object, returning without writing anything if too small
  if (n < 2) {
    return std::nullopt;
  }

  // In a single forward pass, iterate through all top-level object properties,
  // iteratively writing `<json-encoded-name>:<json-encoded-value>[,]` (leaving space
  // for the enclosing braces) until we reach a property whose name and value can't fit
  // in the remaining buffer space. As with other TryEncodeJson implementations, we drop
  // top-level properties from the back: we don't recurse into nested subobjects.
  char* ptr = dst;
  char* const dst_end = dst + n;

  // Write an opening brace
  *ptr++ = '{';

  // Iterate over properties, stopping when we hit one that can't fit
  size_t props_written = 0;
  for (size_t i = 0; i < num_object_properties; ++i) {
    // Get the name and value of the top-level property at this position
    const int index = static_cast<int>(i);
    const std::string_view name = value.GetObjectPropertyNameAt(index);
    const Attribute& val = value.GetObjectPropertyValueAt(index);

    // Determine how many bytes would be occupied by this property if we appended it
    const size_t num_comma_bytes = props_written > 0 ? 1 : 0;
    const size_t num_bytes_to_append =
        num_comma_bytes + GetJsonSize(name) + 1 + GetJsonSize(val);

    // We need to reserve an extra byte to ensure there's space for the closing '}'; if
    // we can't append this property _and_ close the object, we're done
    const size_t num_bytes_needed = num_bytes_to_append + 1;
    if (static_cast<size_t>(dst_end - ptr) < num_bytes_needed) {
      break;
    }

    // Prepend a comma if needed, then the property name and value (names may contain
    // characters that need escaping, hence the use of GetJsonSize/WriteJson for the
    // name as well as the value)
    if (props_written > 0) {
      *ptr++ = ',';
    }
    ptr += WriteJson(ptr, dst_end - ptr, name);
    *ptr++ = ':';
    ptr += WriteJson(ptr, dst_end - ptr, val);
    ++props_written;
  }

  // Write the closing brace: our num_bytes_needed check ensures that we have space
  DATADOG_ASSERT(ptr < dst_end, "attempting to write closing brace past dst_end");
  *ptr++ = '}';

  // Return a result that indicates how much data we wrote and whether we had to drop
  // any attributes
  const size_t num_bytes_written = static_cast<size_t>(ptr - dst);
  const bool truncated = props_written < num_object_properties;
  return AttributeEncodeResult{num_bytes_written, truncated};
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
