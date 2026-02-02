// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/json.hpp"

namespace datadog::impl {

/**
 * This file implements JSON serialization support for arbitrary data structures.
 *
 * Given any data structure:
 *
 *   struct MyEvent {
 *     std::string type;
 *     UUID id;
 *     Timestamp timestamp;
 *     std::optional<std::string> tags;
 *   };
 *
 * You can separately define how a value of that type should be serialized as a JSON
 * object, given a set of JSON-serializable member values:
 *
 *   DATADOG_JSON_STRUCT(
 *     MyEvent,
 *     DATADOG_JSON_FIELD(type),
 *     DATADOG_JSON_FIELD(id),
 *     DATADOG_JSON_FIELD(timestamp),
 *     DATADOG_JSON_FIELD_NAME(tags, "ddtags")
 *   )
 *
 * Property names will match the member variable name exactly. To explicitly specify a
 * different name, use `DATADOG_JSON_FIELD_NAME` and provide a string literal. Property
 * names are NOT escaped: they are assumed to contain only printable ASCII characters,
 * with no backslashes, double-quotes, or control codes.
 *
 * Once a type is annotated with `DATADOG_JSON_STRUCT`, you can serialize any value of
 * that type as a JSON object by passing it to `EncodeJson`:
 *
 *   std::vector<uint8_t> bytes;
 *   MyEvent ev{"foo", UUID::Random(), std::chrono::system_clock::now(), std::nullopt};
 *   EncodeJson(bytes, ev);
 *   std::cout << std::string_view(bytes.data(), bytes.size()) << "\n";
 *
 * The example above will print something like:
 *
 *   {"type":"foo","id":"e9d4be4a-9063-40bc-977d-b073e39d4105","timestamp":"2025-10-24T12:46:17.301Z","ddtags":null}
 *
 * Properties are guaranteed to be serialized in the order in which they were declared.
 * JSON values are guaranteed to be minified: no whitespace or pretty-printing.
 */

/**
 * For use within `DATADOG_JSON_STRUCT`: defines a member variable that should be
 * serialized as a JSON object property. Evaluates to a `std::pair<std::string_view, T>`
 * value. The variadic list of types formed by all such values will match the
 * `template <typename... Fields>` overloads defined below.
 *
 * `Member` is the name of the chosen member variable.
 *
 * `Name` is a string literal denoting the JSON property name to associate with this
 * member. The string value must be unique among all fields of the same struct, and it
 * must contain no double-quotes, backslashes, or control codes.
 */
#define DATADOG_JSON_FIELD_NAME(Member, Name) \
  std::make_pair(std::string_view(Name), (obj.Member))

/**
 * Shorthand used in `DATADOG_JSON_STRUCT` to define JSON-serializable fields for member
 * variables whose names exactly match the desired JSON property name.
 */
#define DATADOG_JSON_FIELD(Member) DATADOG_JSON_FIELD_NAME(Member, #Member)

/**
 * May be added to a field list within `DATADOG_JSON_STRUCT` in order to reserve a field
 * name for future use. When used within `DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES`,
 * any extra attributes with this field's name will be filtered out of the final object.
 */
#define DATADOG_JSON_RESERVED_FIELD(Member) \
  std::make_pair(std::string_view(#Member), std::monostate{})

/**
 * Defines the JSON object format used to serialize a value of type `Type`.
 *
 * Declares inline definitions of `GetJsonSize` and `WriteJson` for the given type.
 * Those implementations forward to compiler-generated overloads of the
 * `template <typename... Fields>` functions defined below, based on the set of fields
 * defined via `DATADOG_JSON_FIELD`.
 */
#define DATADOG_JSON_STRUCT(Type, ...)                                            \
  inline size_t GetJsonSize(const Type& obj) { return GetJsonSize(__VA_ARGS__); } \
  inline size_t WriteJson(char* dst, size_t n, const Type& obj) {                 \
    return WriteJson(dst, n, __VA_ARGS__);                                        \
  }

/**
 * Given a set of fields declared via DATADOG_JSON_FIELD, with each field type having
 * the form `std::pair<std::string_view, T>`, returns the total number of bytes required
 * to encode the given values for those fields as a JSON object.
 */
template <typename... Fields>
size_t GetJsonSize(const Fields&&... fields) {
  // An empty struct is simply encoded as '{}'
  if constexpr (sizeof...(Fields) == 0) {
    return 2;
  }

  // 2 bytes for braces, N-1 bytes for commas, N bytes for colons
  size_t size = 2;                // {}
  size += sizeof...(Fields) - 1;  // commas
  size += sizeof...(Fields);      // colons

  // For each field (std::pair<std::string_view, T>), accumulate the size of the name
  // with enclosing quotes, and the size of the value when JSON-encoded, unless the
  // field's current value indicates that it should be entirely omitted
  ((size += HasJsonValue(fields.second)
                ? (fields.first.size() + 2 + GetJsonSize(fields.second))
                : 0),
   ...);
  return size;
}

/**
 * Given a set of fields declared via DATADOG_JSON_FIELD, serializes a JSON object
 * containing each of those fields' names and values.
 */
template <typename... Fields>
size_t WriteJson(char* dst, size_t n, const Fields&&... fields) {
  char* ptr = dst;
  *ptr++ = '{';

  // Use a lambda fold expression to process each field sequentially
  bool first = true;
  (([&] {
     // If this field's current value is one that should be entirely omitted from the
     // JSON object, skip it
     if (!HasJsonValue(fields.second)) {
       return;
     }

     // Write a comma for all fields after the first one serialized
     if (!first) {
       *ptr++ = ',';
     }
     first = false;

     // Write the name of the field as a string, enclosed in quotes. As cautioned above,
     // we expect that field names do NOT require escaping, as this code deals
     // exclusively with internally-defined data types that should not have arbitrary
     // quotes/slashes/etc. in their property names.
     *ptr++ = '"';
     std::memcpy(ptr, fields.first.data(), fields.first.size());
     ptr += fields.first.size();
     *ptr++ = '"';

     // Write a colon to delimit name and value
     *ptr++ = ':';

     // Write the value as a JSON literal, resolving the appropriate overload for the
     // field's value type
     ptr += WriteJson(ptr, dst + n - ptr, fields.second);
   }()),
   ...);

  *ptr++ = '}';
  return ptr - dst;
}

/**
 * Defines the JSON object format used to serialize a value of type `Type`, merging any
 * custom user-specified property values specified via an Attribute member whose name
 * is given as `Extra`.
 *
 * The resulting serialization routines work identically to `DATADOG_JSON_STRUCT`,
 * except that if the member specified by `Extra` is an Attribute with one or more
 * object property values, those property values will be merged into the resulting JSON
 * object at top-level, alongside the ordinary struct members declared via
 * `DATADOG_JSON_FIELD`. If the `Extra` object contains any properties with names that
 * conflict with the names specified as struct fields, those values will be omitted.
 *
 * The extra `Attribute` field should _NOT_ be listed in the set of `DATADOG_JSON_FIELD`
 * declarations; it should instead be provided as `Extra`, without being wrapped in any
 * macro.
 */
#define DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(Type, Extra, ...)      \
  inline size_t GetJsonSize(const Type& obj) {                           \
    return GetJsonSizeWithExtraAttributes(obj.Extra, __VA_ARGS__);       \
  }                                                                      \
  inline size_t WriteJson(char* dst, size_t n, const Type& obj) {        \
    return WriteJsonWithExtraAttributes(obj.Extra, dst, n, __VA_ARGS__); \
  }

/**
 * Returns the worst-case buffer size required to JSON-serialize a set of struct fields
 * along with the values specified in the given `extra` object.
 */
template <typename... Fields>
size_t GetJsonSizeWithExtraAttributes(
    const Attribute& extra, const Fields&&... fields
) {
  // Compute the size required to encode all our struct fields as a JSON object, without
  // any extra attributes merged in; and use that value as-is if we have no extra fields
  const size_t base_size = GetJsonSize(std::forward<const Fields>(fields)...);
  if (extra.GetObjectPropertyCount() == 0) {
    return base_size;
  }

  // If we've been provided with extra fields, extend our reserved size to also fit the
  // full, JSON-encoded representation of those fields as their own object, with the
  // leading brace trimmed off: this does not account for any extra properties with
  // reserved field names (which will be filtered out in WriteJsonWithExtraAttributes),
  // but it's OK if we overshoot slightly in edge cases
  return base_size + GetJsonSize(extra) - 1;
}

/**
 * Serializes the given set of struct fields to JSON, merging in any `extra` attribute
 * attribute values that are safe to merge. A value is safe to merge if its property
 * name does not conflict with any of the struct field names given in `fields`.
 */
template <typename... Fields>
size_t WriteJsonWithExtraAttributes(
    const Attribute& extra, char* dst, size_t n, const Fields&&... fields
) {
  // Use the normal struct serialization implementation to produce a JSON object with
  // the base fields of our struct, e.g. `{"id":"foo","name":"bar"}`. (Even in the null
  // case, this value will still be a valid JSON object, e.g. `{}`.)
  const size_t base_size = WriteJson(dst, n, std::forward<const Fields>(fields)...);
  DATADOG_ASSERT(
      base_size >= 2 && dst[0] == '{' && dst[base_size - 1] == '}',
      "WriteJson for struct produced non-object value"
  );

  // If we have no extra attributes whatsoever, early-out and return the base object
  // as-is: no need to bother with filtering or concatenation
  if (extra.GetObjectPropertyCount() == 0) {
    return base_size;
  }

  // It's possible that the set of `extra` attributes includes one or more top-level
  // property values that overlap with the set of canonical property names specified in
  // `fields`. Prepare a constexpr function that will return true if a given attribute
  // name is safe for merging; false if it conflicts with a value in `fields`.
  auto is_safe_name = [&](std::string_view name) constexpr {
    return !((name == std::forward<const Fields>(fields).first) || ...);
  };

  // Advance to the last character of the base object to begin writing extra properties
  char* extra_start = dst + base_size - 1;
  DATADOG_ASSERT(*extra_start == '}', "Bad write position for extra properties");

  // Write extra properties, overlapping the original JSON value by one, then convert
  // the extra object's opening brace to a comma to concatenate the two
  const size_t extra_size =
      WriteFilteredJsonObject(extra_start, n - base_size + 1, extra, is_safe_name);
  DATADOG_ASSERT(
      extra_size >= 2 && *extra_start == '{' && *(extra_start + extra_size - 1) == '}',
      "WriteFilteredJsonObject produced non-object value"
  );

  // Note that in a case where `extra` contains only properties with reserved names, it
  // could write `{}`, producing `{"id":"foo","name":"bar"{}`, which would not be valid
  // JSON if we turned it into `{"id":"foo","name":"bar",}`. We need to handle this case
  // specifically, after we serialize the extra attributes, since catching it beforehand
  // would require iterating over all property values up-front to perform a separate
  // filtering pass.
  if (extra_size == 2) {
    // Revert the last character to a closing brace, then return the original size: JSON
    // values are not null-terminated, so we can leave the extra '}' alone
    *extra_start = '}';
    return base_size;
  }

  // Otherwise, it's guaranteed that both our base value and our extra value were
  // encoded as JSON object values with at least 1 property, so replacing the extra
  // value's opening '{' (which was originally the base value's closing '}') with a
  // comma will correctly concatenate the two values into a single object
  *extra_start = ',';

  // Return the total size of both values, accounting for the fact that we overlapped
  // them by one byte
  return base_size + extra_size - 1;
}

}  // namespace datadog::impl
