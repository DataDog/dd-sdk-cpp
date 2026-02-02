// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <variant>

namespace datadog::impl {

/**
 * Returns the length of a monostate value when encoded to JSON. Since std::monostate is
 * used as a placeholder value for reserved field names, this function always returns 0.
 */
inline size_t GetJsonSize(const std::monostate&) { return 0; }

/**
 * Encodes a monostate value to to JSON. Since std::monostate is used as a placeholder
 * value for reserved field names, this function is always a no-op.
 */
inline size_t WriteJson(char*, size_t, const std::monostate&) { return 0; }

/**
 * Specifies that a property of type std::monostate should never be included when its
 * parent value is serialized as a JSON object. Since std::monostate is used as a
 * placeholder value for reserved field names, it is always omitted from serialization.
 */
inline bool HasJsonValue(const std::monostate&) { return false; }

}  // namespace datadog::impl
