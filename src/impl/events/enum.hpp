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

namespace datadog::impl {

/**
 * Given an enum type T, e.g.:
 *
 *   enum class Tag : uint8_t { Foo, Bar };
 *
 * ...and a list of names that correspond 1:1 with that type's enum values:
 *
 *   constexpr std::string_view TagNames[] = {"foo", "bar"};
 *   static_assert(TagNames[static_cast<size_t>(Type::Foo)] == "foo", "");
 *   static_assert(TagNames[static_cast<size_t>(Type::Bar)] == "bar", "");
 *
 * Then defining a `StringEnum` specialization will result in a wrapper type for T that
 * will be serialized to JSON as a string.
 *
 *   using StringTag = StringEnum<Tag, TagNames, std::size(TagNames)>;
 *   StringTag tag = Tag::Foo;
 *   EncodeJson(buf, tag); // Populates buf with the JSON string "foo"
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
template <typename T, std::string_view const* Names, size_t N>
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
    return Names[index];  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  }
};

template <typename T, std::string_view const* Names, size_t N>
size_t GetJsonSize(const StringEnum<T, Names, N>& value) {
  return value.Name().size() + 2;
}

template <typename T, std::string_view const* Names, size_t N>
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
