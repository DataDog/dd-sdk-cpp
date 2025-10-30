// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "assert.hpp"
#include "attribute/merge.hpp"
#include "datadog/attribute.hpp"

// clang-format off
#include "json/primitives/bool.hpp"
#include "json/primitives/float.hpp"
#include "json/primitives/integer.hpp"
#include "json/primitives/null.hpp"
#include "json/primitives/string.hpp"
#include "json/primitives/timestamp.hpp"
#include "json/primitives/uuid.hpp"
#include "json/attribute.hpp"
#include "json/optional.hpp"
// clang-format on

namespace datadog::impl {

/**
 * This file is the top-level header for the Datadog SDK's JSON serialization support.
 * To ensure consistent definition of function overloads etc., any TU that needs to
 * encode values as JSON should include this header and no other `json/` header.
 *
 * To encode a value of any JSON-serializable type, use `EncodeJson`.
 *
 * The files within in `src/impl/json/` define JSON serialization functions for all
 * supported types. For each type, those definitions consists of three functions:
 *
 * size_t datadog::impl::GetJsonSize(const T& value):
 *   Given a value of type T, returns the number of bytes required to store the
 *   JSON-encoded string representation of that value. In many cases, the result is an
 *   exact-fit size, but the worst-case maximum size may be returned in cases where it's
 *   impractical to compute a precise size (e.g. floats).
 *
 * size_t datadog::impl::WriteJson(char* dst, size_t n, const T& value):
 *   Given a value of type T and an output buffer `dst` with `n` bytes available, writes
 *   the given value to the buffer as a JSON-encoded string. Returns the number of bytes
 *   written. Does not perform bounds checking, except for fatal assertions when
 *   WITH_DATADOG_ASSERTS is enabled: required buffer size should always be precomputed
 *   via GetJsonSize().
 *
 * bool datadog::impl::HasJsonValue(const T& value):
 *   Given a value of type T, indicates whether that value should be serialized to JSON
 *   at all. This function is used when serializing a data structure to a JSON object,
 *   in order to support properties that should be omitted from the object entirely when
 *   they contain a null or zero value.
 *
 * JSON values must be minified: no whitespace or pretty-printing.
 */

/**
 * Default template implementation for HasJsonValue: if a type does not have an overload
 * for this function, a value of that type is assumed to always be present when used as
 * a JSON object property.
 */
template <typename T>
bool HasJsonValue(const T&) {
  return true;
}

/**
 * Given a value of type T, serializes it to the given buffer in JSON format.
 */
template <typename T>
void EncodeJson(std::vector<uint8_t>& out_buffer, const T& value) {
  // Ensure that our buffer has space to fit this value when JSON-serialized
  const size_t precomputed_size = GetJsonSize(value);
  out_buffer.resize(precomputed_size);

  // Serialize our value into the buffer
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  char* dst = reinterpret_cast<char*>(out_buffer.data());
  const size_t num_bytes_written = WriteJson(dst, precomputed_size, value);

  // Verify that WriteJson never writes more data than GetJsonSize indicates we need
  DATADOG_ASSERT(
      num_bytes_written <= precomputed_size, "unexpected overflow of JSON buffer"
  );

  // Ensure that out_buffer is bounded to include only the data we've written, in case
  // we overestimated buffer size
  out_buffer.resize(num_bytes_written);
}

/**
 * Serializes an event payload to JSON, along with a set of custom user-supplied
 * attributes, according to the format used by log events. Namely:
 *
 * - User attributes from multiple levels (e.g. global, logger, etc.) are merged at the
 *   time the event is encoded
 * - All user attributes are merged into the event payload at top-level
 * - User attribute names are unchanged, hence it's possible for a user attribute to
 *   conflict with a statically-defined property of the event payload type unless all
 *   reserved property names are specified via `filter_func`
 *
 * @param out_buffer - The destination buffer. The calling thread must have exclusive
 *  access to this vector.
 * @param value - The event payload value to be encoded. Must be a value that is
 *  JSON-serialized to an object containing at least one property.
 * @param mut_attribute - A mutable `Attribute` value of type `Object`, used to contain
 *  the result of merging all values in `user_attributes`.
 * @param user_attributes - A list of `Attribute` values of type `Object`, each encoding
 *  a set of user-specified attributes to be merged in. Where names conflict, values
 *  that appear further right in the list take precedence.
 * @param filter_func - A static function that returns true if an attribute of the given
 *  name is safe to merge into the resulting JSON object; false if the name is reserved
 *  and the value should be excluded.
 */
template <typename T>
void EncodeJsonWithMergedUserAttributes(
    std::vector<uint8_t>& out_buffer,
    const T& value,
    Attribute& mut_attribute,
    std::initializer_list<Attribute> user_attributes,
    bool (*filter_func)(std::string_view)
) {
  // Preemptively merge our Attribute values into a single object, excluding any values
  // that use reserved property names
  AttributeMerge::AssembleObject(mut_attribute, user_attributes, filter_func);

  // Determine how many bytes we need in order to encode the base value as a JSON object
  const size_t encoded_value_size = GetJsonSize(value);

  // Determine how many bytes we need in order to encode the merged set of user
  // attributes as a JSON object
  size_t encoded_attributes_size = 0;
  if (mut_attribute.GetType() == ValueType::Object) {
    encoded_attributes_size = GetJsonSize(mut_attribute);
    DATADOG_ASSERT(encoded_attributes_size >= 2, "got JSON object size < 2");
  }

  // For a base value of size N and a merged attributes object of size M, we need
  // (N + M - 1) bytes in total, since we'll overlap the values by one byte. To avoid
  // branching on whether either is an empty object, we allocate N + M bytes
  // (overshooting by 1 in the common case), since we'll trim to fit after writing
  // regardless.
  const size_t combined_buffer_size = encoded_value_size + encoded_attributes_size;

  // Reserve space in the buffer for the full payload, including attributes
  out_buffer.resize(combined_buffer_size);

  // Write our base value to the buffer as a complete JSON object, without attributes
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  char* dst = reinterpret_cast<char*>(out_buffer.data());
  const size_t num_value_bytes_written = WriteJson(dst, encoded_value_size, value);
  DATADOG_ASSERT(num_value_bytes_written <= encoded_value_size, "JSON overflow");

  // Verify that we actually wrote a JSON object greater than 2 bytes in length: we
  // should only be calling this function for event types that are unconditionally
  // rendered as non-empty objects when JSON-serialized
  if (num_value_bytes_written <= 2 || static_cast<char>(out_buffer[0]) != '{') {
    DATADOG_ASSERT(
        false,
        "attempted to merge attributes with a value that does not serialize to JSON as "
        "a non-empty object"
    );
    // Safely bail out in production builds: write an empty object and make no attempt
    // to include attributes
    out_buffer.resize(2);
    out_buffer[0] = static_cast<uint8_t>('{');
    out_buffer[1] = static_cast<uint8_t>('}');
    return;
  }

  // If our set of merged user attributes is empty, early-out: we've written a complete
  // event payload already and there's nothing more to write
  if (encoded_attributes_size <= 2) {
    out_buffer.resize(num_value_bytes_written);
    return;
  }

  // We have user attributes to merge into our base event payload. Our AttributeMerge
  // call already excluded values with reserved names, so we can simply concatenate the
  // two JSON payloads. Given a base event of `{"foo":1,"bar":2}` and merged user
  // attributes `{"baz":3}`, the current state of the buffer is:
  //
  // {"foo":1,"bar":2}........
  //
  // Since we know that we're dealing with two JSON object values, each of which has at
  // least one property, we can write the second object at the position of the final
  // byte in the first:
  //
  // {"foo":1,"bar":2{"baz":3}
  //
  dst += num_value_bytes_written - 1;
  const size_t num_bytes_remaining = combined_buffer_size - num_value_bytes_written + 1;
  DATADOG_ASSERT(
      static_cast<void*>(dst + num_bytes_remaining) <=
          static_cast<void*>(out_buffer.data() + out_buffer.capacity()),
      "computed incorrect remaining buffer size for merged attribute write"
  );
  const size_t num_attribute_bytes_written =
      WriteJson(dst, num_bytes_remaining, mut_attribute);
  DATADOG_ASSERT(num_attribute_bytes_written <= num_bytes_remaining, "JSON overflow");

  // Next, we simply clobber the opening brace of our second JSON object with a comma,
  // and we've effectively concatenated the two objects:
  //
  // {"foo":1,"bar":2,"baz":3}
  //                 ^
  *dst = ',';

  // Finally, we need to ensure that our buffer is sized exactly to fit the bytes we've
  // written
  const size_t num_bytes_written =
      num_value_bytes_written + num_attribute_bytes_written - 1;
  out_buffer.resize(num_bytes_written);
}

}  // namespace datadog::impl
