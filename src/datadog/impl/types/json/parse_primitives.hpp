// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Parses a JSON boolean literal (`true` or `false`). Returns false if neither.
 */
bool ParseJsonBool(std::string_view json_literal, bool& out);

/**
 * Parses an unquoted JSON integer literal into an int64_t.
 */
bool ParseJsonInt64(std::string_view json_literal, int64_t& out);

/**
 * Parses an unquoted JSON integer literal into a uint64_t.
 */
bool ParseJsonUInt64(std::string_view json_literal, uint64_t& out);

/**
 * Parses an unquoted JSON number literal (integer or decimal) into a double. Uses
 * std::from_chars with chars_format::general.
 */
bool ParseJsonDouble(std::string_view json_literal, double& out);

/**
 * Parses a JSON string literal containing a 36-character UUID
 * (e.g. `"xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"`). Returns false if malformed or not a
 * valid UUID.
 */
bool ParseJsonUUID(std::string_view json_literal, UUID& out);

/**
 * Parses a quoted JSON string literal (including enclosing double-quotes and any escape
 * sequences) into a std::string. Returns false if the literal is malformed. Handles all
 * single-character escapes and \u00XX; rejects other \uXXXX sequences.
 */
bool ParseJsonString(std::string_view json_literal, std::string& out);

}  // namespace datadog::impl
