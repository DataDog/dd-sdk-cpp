// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>

namespace datadog::impl {

/**
 * Returns the number of bytes required to serialize a literal 'true' or 'false'.
 */
size_t GetJsonSize(const bool& value);

/**
 * Serializes a literal 'true' or 'false'.
 */
size_t WriteJson(char* dst, size_t n, const bool& value);

}  // namespace datadog::impl
