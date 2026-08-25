// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/types/json.hpp"

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

  // Allow implicit conversion and assignment from T or anything convertible to T
  // NOLINTNEXTLINE(google-explicit-constructor)
  template <
      typename U,
      typename = std::enable_if_t<
          std::is_constructible_v<T, U> &&
          !std::is_same_v<std::decay_t<U>, Omissible<T>>>>
  Omissible(U&& in_value) : value(static_cast<T>(std::forward<U>(in_value))) {}

  template <
      typename U,
      typename = std::enable_if_t<
          std::is_assignable_v<T&, U> &&
          !std::is_same_v<std::decay_t<U>, Omissible<T>>>>
  Omissible& operator=(U&& in_value) {
    value = static_cast<T>(std::forward<U>(in_value));
    return *this;
  }
};

/**
 * Specialization for Omissible<T> that considers a property to be without a value when
 * its current value is equivalent to the default for that type.
 *
 * Overrides the default HasJsonValue() implementation from `json.hpp`, which
 * unconditionally returns true.
 */
template <typename T>
bool HasJsonValue(const Omissible<T>& value) {
  // If we're holding a value that's equivalent to the default value of our underlying
  // type, return false: this lets the implementation of DATADOG_JSON_STRUCT know that
  // there no need to serialize this value as a property
  return value.value != T{};
}

/**
 * Specialization for Omissible<std::optional<T>>, which treats `std::nullopt` as the
 * case where the property should be omitted, preserving the zero-value as significant.
 */
template <typename T>
bool HasJsonValue(const Omissible<std::optional<T>>& value) {
  const std::optional<T>& opt = value.value;
  return opt.has_value();
}

/**
 * Specialization for Omissible<std::vector<T>>, which treats an empty vector as the
 * case where the property should be omitted. This avoids requiring T to define
 * operator== just to satisfy the generic `value != T{}` check.
 */
template <typename T>
bool HasJsonValue(const Omissible<std::vector<T>>& value) {
  return !value.value.empty();
}

/**
 * Specialization for Omissible<Attribute>, which treats `Attribute::Null()` as the
 * case where the property should be omitted. A default-constructed `Attribute` _is_
 * equivalent to `Attribute::Null()`, but `Attribute` does not define comparison
 * operators.
 */
inline bool HasJsonValue(const Omissible<Attribute>& value) {
  return value.value.GetType() != ValueType::Null;
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

// Type aliases for use in struct declarations, to make the expected serialization
// behavior more apparent:

template <typename T>
using OmitIfZero = Omissible<T>;

template <typename T>
using OmitIfFalse = Omissible<T>;

template <typename T>
using OmitIfEmpty = Omissible<T>;

template <typename T>
using OmitIfNoValue = Omissible<std::optional<T>>;

}  // namespace datadog::impl
