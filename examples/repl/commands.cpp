// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "repl/commands.hpp"

#include <optional>

#include "datadog.hpp"

std::optional<datadog::Attribute> CollectAttributes(const NamedValueList& named) {
  // Count attr: entries first so we can size the object up-front
  size_t count = 0;
  for (size_t i = 0; i < named.n; i++) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    if (named.values[i].name == "attr") {
      count++;
    }
  }
  if (count == 0) {
    return std::nullopt;
  }

  datadog::Attribute result = datadog::Attribute::Object(count);
  for (size_t i = 0; i < named.n; i++) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    if (named.values[i].name != "attr") {
      continue;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    std::string_view kv = named.values[i].value;
    const size_t colon = kv.find(':');
    std::string_view attr_key = kv.substr(0, colon);
    std::string_view attr_val =
        colon != std::string_view::npos ? kv.substr(colon + 1) : std::string_view{};
    result.SetObjectProperty(attr_key, datadog::Attribute::String(attr_val));
  }
  return result;
}
