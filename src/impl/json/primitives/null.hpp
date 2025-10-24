// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>

namespace datadog::impl {

/**
 * Returns the length of a literal JSON null value.
 */
inline size_t GetJsonSize(const std::nullptr_t&) { return 4; }

/**
 * Writes a literal JSON null value to the provided buffer.
 */
size_t WriteJson(char* dst, size_t n, const std::nullptr_t&);

}  // namespace datadog::impl
