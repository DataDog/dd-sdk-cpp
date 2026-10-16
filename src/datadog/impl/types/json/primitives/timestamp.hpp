// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "datadog/timestamp.hpp"

namespace datadog::impl {

/**
 * Returns the exact number of bytes required to encode the given string value as a JSON
 * string literal, including enclosing quotes and escape characters.
 */
size_t GetJsonSize(const Timestamp& value);

/**
 * Encodes the given string value as a quoted, escaped JSON string literal.
 */
size_t WriteJson(char* dst, size_t n, const Timestamp& value);

}  // namespace datadog::impl
