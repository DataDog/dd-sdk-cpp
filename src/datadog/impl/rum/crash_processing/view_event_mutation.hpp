// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>

#include "datadog/impl/rum/crash_processing/view_event_parser.hpp"

namespace datadog::impl {

/**
 * Given the JSON-encoded text of a RUM View event, and the result of successfully
 * parsing that event via RumViewEventParser, returns a string containing a new RUM View
 * event describing the same view, exactly as the original event described it, but with
 * a subset of fields updated or added in order to reflect that a crash occurred in that
 * view.
 *
 * The provided `spans` and `values` MUST be obtained following a successful call to
 * `RumViewEventParser::Parse()`.
 *
 * The resulting view event will carry the same set of property values as
 * `view_event_json`, with these changes:
 *
 * - `date` is updated to `crash_timestamp_ms - 1`, extending the lifetime of the view
 *   to encompass the moment just before it crashed (while ensuring that crash appears
 *   last in the session timeline)
 * - `view.is_active` is set to false, marking the view as ended by the crash
 * - `view.error.count` is incremented by 1
 * - `view.crash` is inserted, unconditionally set to `{"count":1}`
 * - `_dd.document_version` is incremented, ensuring that the new event supersedes all
 *   previous events describing the same view
 */
std::string MutateViewEventForCrash(
    std::string_view view_event_json,
    const RumViewEventParser::Spans& spans,
    const RumViewEventParser::Values& values,
    uint64_t crash_timestamp_ms
);

}  // namespace datadog::impl
