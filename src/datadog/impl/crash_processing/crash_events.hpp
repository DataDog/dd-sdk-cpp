// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <functional>

namespace datadog::impl {

/**
 * Type of RUM Event that can be produced in response to a crash in the instrumented
 * application. Unless a crash is dropped, it will always produce a new RUM Error event
 * with error.is_crash set to true. A crash will also typically produce a RUM View
 * event (representing either a new view created retroactively to contain the crash, or
 * an updated record of the last view that was active at the time of the crash),
 * although the view event may be omitted if it's been more than 4 hours since the
 * last-active view was last updated.
 */
enum class CrashEventType : uint8_t { View, Error };

/**
 * Function that accepts the events that result from processing a crash, in the form of
 * a JSON-encoded object value.
 *
 * When the SDK build is configured to handle crashes in-process, the resulting events
 * are pushed directly onto the storage queue to be written to disk in the latest batch
 * file for RUM.
 *
 * When the SDK is configured to use Crashpad for crash handling, the Crashpad handler
 * process writes these events to the map of annotations used as form-field values
 * (along with the minidump file) in its HTTP POST request.
 */
using CrashEventSink = std::function<bool(CrashEventType, std::string_view)>;

}  // namespace datadog::impl
