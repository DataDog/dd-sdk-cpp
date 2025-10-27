// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "assert.hpp"
#include "json.hpp"

namespace datadog::impl {

/**
 * Wrapper for a value of type T for use in struct types. When a struct value is
 * serialized to JSON, if this value is zero, it will be entirely omitted from the
 * resulting JSON object.
 *
 * The value will be serialized normally if `value != T{}`; otherwise it will be
 * omitted. To customize the criteria for omitting a value of some type `T`, define an
 * overload of `HasJsonValue` for an `Omissible` value of that type, e.g.:
 *
 *   bool HasJsonValue(const Omissible<MyType>& value) {
 *     return !value.value.IsEmpty();
 *   }
 */
template <typename T>
struct Omissible {
  T value;

  // T is assumed to be default-constructible: comparison to default value is how we
  // determine whether the value should be included or omitted
  Omissible() {}

  // Allow implicit conversion and assignment from T
  // NOLINTNEXTLINE(google-explicit-constructor)
  Omissible(const T& in_value) : value(in_value) {}
  Omissible& operator=(const T& in_value) {
    value = in_value;
    return *this;
  }
};

template <typename T>
bool HasJsonValue(const Omissible<T>& value) {
  // If we're holding a value that's equivalent to the default value of our underlying
  // type, return false: this lets the implementation of DATADOG_JSON_STRUCT know that
  // there no need to serialize this value as a property
  return value.value != T{};
}

template <typename T>
size_t GetJsonSize(const Omissible<T>& value) {
  // HasJsonValue will pre-empt the normal struct serialization path; there's no need to
  // branch based on value here
  return GetJsonSize(value.value);
}

template <typename T>
size_t WriteJson(char* dst, size_t n, const Omissible<T>& value) {
  // Ditto; just defer to the WriteJson implementation for the underlying value type
  return WriteJson(dst, n, value.value);
}

}  // namespace datadog::impl
