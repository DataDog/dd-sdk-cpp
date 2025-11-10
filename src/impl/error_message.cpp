// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "error_message.hpp"

#include <array>
#include <cstring>
#include <new>

#include "assert.hpp"
#include "json.hpp"

namespace datadog::impl {

ErrorMessage::ErrorMessage(const char* in_text, const Attribute& in_attributes)
    : text(in_text), attributes(in_attributes), num_prefixes(0), prefixes() {
  DATADOG_ASSERT(text, "ErrorMessage must have non-null text");
}

ErrorMessage::ErrorMessage(
    const char* in_text,
    std::initializer_list<std::pair<std::string_view, Attribute>> in_attributes
)
    : text(in_text), num_prefixes(0), prefixes() {
  DATADOG_ASSERT(text, "ErrorMessage must have non-null text");
  if (in_attributes.size() > 0) {
    attributes.InitObject(in_attributes.size());
    for (const auto& kvp : in_attributes) {
      attributes.SetObjectProperty(kvp.first, kvp.second);
    }
  }
}

ErrorMessage& ErrorMessage::AddPrefix(const char* prefix) {
  DATADOG_ASSERT(prefix, "ErrorMessage must have non-null prefix");
  if (num_prefixes < prefixes.max_size()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    prefixes[num_prefixes++] = prefix;
  }
  return *this;
}

std::string ErrorMessage::Format() const {
  // Accumulate the required size of our final string, without null terminator
  const std::string_view own_text = text;
  size_t len = own_text.size();

  // Add size required to prepend prefixes
  if (num_prefixes > 0) {
    len += 2 * num_prefixes;  // ': ' appended to each prefix
    for (size_t i = 0; i < num_prefixes; i++) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
      len += prefixes[i].size();
    }
  }

  // Add size required to append attribute values
  const bool has_attributes = attributes.GetObjectPropertyCount() > 0;
  if (has_attributes) {
    len += 1;  // ' ' to delimit
    len += GetJsonSize(attributes);
  }

  // Prepare a string with a buffer large enough for our full message
  std::string s;
  s.reserve(len);

  // Append each prefix, back-to-front
  for (int i = static_cast<int>(num_prefixes) - 1; i >= 0; --i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    s += prefixes[i];
    s += ": ";
  }

  // Append inner error message text
  s += own_text;

  // Append attribute values as a JSON value
  if (has_attributes) {
    // Add a single space to delimit text from attribute values
    s += ' ';

    // Get a pointer to the remainder of the buffer after all the static text we've just
    // written
    const size_t static_text_len = s.size();
    char* dst = s.data() + static_text_len;

    // Resize the string to its full capacity so we can write directly to the buffer,
    // then serialize our JSON attribute values
    s.resize(len);
    char* const end = s.data() + s.size();
    const size_t json_len = WriteJson(dst, end - dst, attributes);
    DATADOG_ASSERT(
        dst + json_len <= end, "ErrorMessage wrote attribute values past end of buffer"
    );

    // Constrict the string to its final size, if we overshot
    s.resize(static_text_len + json_len);
  }

  // Return our new string
  return s;
}

}  // namespace datadog::impl
