// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>

#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Returns the exact number of bytes required to encode a UUID value as a JSON string,
 * including quotes.
 */
size_t GetJsonSize(const UUID& value);

/**
 * Encodes the given UUID value as a quoted JSON string literal, lowercase-hex-encoded.
 */
size_t WriteJson(char* dst, size_t n, const UUID& value);

}  // namespace datadog::impl
