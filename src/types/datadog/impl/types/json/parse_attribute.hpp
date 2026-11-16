// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/attribute.hpp"

namespace datadog::impl {

/**
 * Builds an Attribute from a JSON value literal (string, number, bool, null, array, or
 * object). Returns false if the input is not a valid JSON value.
 *
 * Type mapping:
 *   JSON null                         -> Attribute::Null()
 *   JSON true/false                   -> Attribute::Bool()
 *   JSON integer (no '.' or 'e'/'E'):
 *     fits int64_t                    -> Attribute::Int()
 *     fits uint64_t only              -> Attribute::UInt()
 *     otherwise                       -> <failure>
 *   JSON number with '.' or 'e'/'E'   -> Attribute::Double() via from_chars general
 *   JSON string                       -> Attribute::String() (unescaped)
 *   JSON array                        -> Attribute::Array(), items recursively parsed
 *   JSON object                       -> Attribute::Object(), values recursively parsed
 *
 * Note that type information is not fully preserved. Timestamp and UUID values are
 * parsed as Attribute::String(), while Attribute::Double(1.0) (serialized to "1") would
 * be recognized as Attribute::Int(1).
 *
 * Our use case for this parsing code is to reconsitute `Attribute` values representing
 * custom attribute values, so that we can re-serialize them to JSON after merging or
 * other light modifications. This loss of type information is acceptable, since values
 * will be re-serialized to the same JSON format regardless.
 */
bool ParseJsonAttribute(std::string_view json_value, Attribute& out);

}  // namespace datadog::impl
