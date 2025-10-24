// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cinttypes>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "assert.hpp"
#include "json.hpp"

namespace datadog::impl {

/**
 * This file implements JSON serialization support for enumeration types that are
 * serialized as strings.
 *
 * Given any enum type:
 *
 *   enum class Tag : uint8_t { Foo, Bar };
 *
 * You can separately define how that type should be serialized to JSON as a union of
 * possible string values:
 *
 *   DATADOG_STRING_ENUM(
 *     StringTag,
 *     Tag,
 *     DATADOG_ENUM_VALUE(Tag::Foo, "foo"),
 *     DATADOG_ENUM_VALUE(Tag::Bar, "bar")
 *   )
 *
 * Then any variable of type `StringTag` can accept `Tag` values, and serializing the
 * `StringTag` will produce a JSON string:
 *
 *   StringTag tag = Tag::Foo;
 *   EncodeJson(buf, tag);  // Populates buf with the JSON string "foo"
 */

/**
 * For use within `DATADOG_STRING_ENUM`: defines the mapping between a raw enum value
 * and the string used to serialize that value to JSON.
 */
#define DATADOG_ENUM_VALUE(EnumValue, Name) \
  StringEnumValue { static_cast<size_t>(EnumValue), Name }

/**
 * Defines the JSON string values used to serialize an enum value of EnumType, defining
 * a new StringEnum template specialization
 */
#define DATADOG_STRING_ENUM(StringEnumType, EnumType, ...)                           \
  constexpr StringEnumValue EnumType##Names[] = {__VA_ARGS__};                       \
  static_assert(                                                                     \
      StringEnumValuesAreContiguous<std::size(EnumType##Names)>(EnumType##Names),    \
      #StringEnumType " value names are discontiguous with original enum " #EnumType \
  );                                                                                 \
  using StringEnumType =                                                             \
      StringEnum<EnumType, EnumType##Names, std::size(EnumType##Names)>;

/**
 * Helper struct used in conjunction with DATADOG_STRING_ENUM. The index `i` encodes the
 * underlying integer value from the original enum type: this is used at compile-time to
 * validate that the StringEnum type is defined correctly.
 */
struct StringEnumValue {
  size_t i;
  std::string_view name;
};

/**
 * Validates an array of values declared via DATADOG_ENUM_VALUE, at compile-time, to
 * ensure that the range of enum values given is contiguous. This guards against
 * copy-paste errors (duplicate enum names) as well as some cases where the
 * DATADOG_STRING_ENUM annotation is not updated in tandem with the underlying enum.
 *
 * This function does *not* perform any validation on the string name values.
 */
template <size_t N>
constexpr bool StringEnumValuesAreContiguous(const StringEnumValue* values) {
  for (size_t i = 0; i < N; i++) {
    if (values[i].i != i) {
      return false;
    }
  }
  return true;
}

/**
 * Wrapper for a C++ enum type, augmented with the set of string names associated with
 * that enum's values as a compile-time constant. When serialized to JSON, a StringEnum
 * value will be rendered as a JSON string containing the name of the currently-assigned
 * enum value.
 *
 * String values are NOT escaped: names given for enum values are assumed to contain no
 * backslashes, quotes, or control characters; as this code is for use strictly with
 * internally-defined types.
 *
 * Note that this could be simpler in C++20, where we could pass a std::array as a
 * template argument, and simpler still with C++23 reflection. Since C++17 only
 * supports integral values as template arguments, we must use a C-style pointer for our
 * names array; and since a value of type `std::string_view[N]` will unavoidably lose
 * its compile-time size information when it decays to a pointer, we must accept an
 * explicit array size as a separate argument.
 */
template <typename T, const StringEnumValue* Values, size_t N>
struct StringEnum {
  T value;

  // Allow implicit construction and assignment of underlying enum values
  StringEnum(T in_value) : value(in_value) {}  // NOLINT(google-explicit-constructor)
  StringEnum& operator=(T new_value) {
    value = new_value;
    return *this;
  }

  /**
   * Resolves the string name associated with the currently-assigned enum value.
   *
   * If the underlying integer value does not correspond to a valid enum value (such as
   * in the case of an improperly-defined StringEnum type or an unsafe static_cast to
   * the enum type), returns the empty string.
   */
  constexpr std::string_view Name() const {
    const size_t index = static_cast<size_t>(value);
    if (index >= N) {
      return "";
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    return Values[index].name;
  }
};

/**
 * Returns the number of bytes required to serialize the given enumeration value as a
 * JSON string.
 *
 * As cautioned above, we do not escape string values: names for internally-defined enum
 * values are assumed to be JSON-safe as-is.
 */
template <typename T, const StringEnumValue* Names, size_t N>
size_t GetJsonSize(const StringEnum<T, Names, N>& value) {
  return value.Name().size() + 2;
}

/**
 * Serializes the given enumeration value as a JSON string. If the assigned value is not
 * a valid enumeration for the given type, writes an empty string.
 *
 * As cautioned above, we do not escape string values.
 */
template <typename T, const StringEnumValue* Names, size_t N>
size_t WriteJson(char* dst, size_t n, const StringEnum<T, Names, N>& value) {
  std::string_view name = value.Name();
  const size_t name_size = name.size();
  const size_t quoted_string_size = name_size + 2;
  DATADOG_ASSERT(n >= name_size + 2, "insufficient buffer space for StringEnum write");
  dst[0] = '"';
  std::memcpy(dst + 1, name.data(), name.size());
  dst[name_size + 1] = '"';
  return quoted_string_size;
}

}  // namespace datadog::impl
