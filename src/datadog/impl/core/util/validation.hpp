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

/**
 * Validates that a RUM vital.name matches the schema facet-path character set
 * in `_vital-common-schema.json`: letters, digits, and the characters
 * `- _ . @ $`. Returns true when every character is allowed. The backend uses
 * `vital.name` as a facet path, so enforcing this at the API boundary prevents
 * offending events from reaching the intake. operation_key is NOT subject to
 * this rule.
 */
bool HasOnlyAllowedOperationNameCharacters(std::string_view s);

/**
 * Validates a duration (in seconds) passed to a long-task-reporting API: returns true
 * if it is positive, finite, and small enough to convert to a datadog::Duration
 * (nanosecond count) without overflow. Rejects NaN, +/-Infinity, and out-of-range
 * values that would otherwise make the subsequent duration_cast to an integral
 * nanosecond count undefined behavior.
 */
bool IsValidLongTaskDurationSeconds(double duration_seconds);

}  // namespace datadog::impl
