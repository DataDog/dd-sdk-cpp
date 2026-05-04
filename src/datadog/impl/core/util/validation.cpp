// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <cctype>

#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::impl {

bool IsBlankString(std::string_view s) {
  for (unsigned char c : s) {
    if (!std::isspace(c)) {
      return false;
    }
  }
  return true;
}

bool IsBlankCString(const char* s) { return s == nullptr || IsBlankString(s); }

bool HasOnlyAllowedOperationNameCharacters(std::string_view s) {
  for (unsigned char c : s) {
    bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                   c == '@' || c == '$';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

}  // namespace datadog::impl
