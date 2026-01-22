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

}  // namespace datadog::impl
