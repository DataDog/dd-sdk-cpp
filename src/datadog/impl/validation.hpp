// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

namespace datadog::impl {

/**
 * Validation function for string values passed to API functions: returns true if the
 * string is empty or consists entirely of whitespace.
 */
bool IsBlankString(std::string_view s);

/**
 * Validates a C-style string with the same rules as IsBlankString, preemptively
 * returning true if the provided pointer is null.
 */
bool IsBlankCString(const char* s);

}  // namespace datadog::impl
