// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>

#include "datadog/attribute.hpp"

namespace datadog::impl {

/**
 * Returns the number of bytes required to encode the given Attribute value in JSON
 * format.
 */
size_t GetJsonSize(const Attribute& value);

/**
 * Encodes the given Attribute value in JSON format.
 */
size_t WriteJson(char* dst, size_t n, const Attribute& value);

}  // namespace datadog::impl
