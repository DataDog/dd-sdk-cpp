// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

/**
 * Converts a null-terminated string argument from the C API to a string_view, treating
 * NULL as an empty string.
 */
inline std::string_view to_string_view(const char* value) {
  return value ? std::string_view{value} : std::string_view{};
}
