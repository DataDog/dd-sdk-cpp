// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <functional>
#include <optional>
#include <vector>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/types/crash_reporting.hpp"
#include "datadog/impl/types/diagnostics.hpp"

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

/**
 * Entry point for RUM Crash Processing logic: given the details of a crash, including a
 * CrashContext struct that describes the relevant state of the RUM Application at the
 * time of the crash, produces between 0 and 2 RUM events that reflect the fact that the
 * application crashed.
 *
 * If the crash can not or should not be handled, returns false and produces no events.
 *
 * If the crash is successfully handled, `sink` is used to convey one or more RUM Events
 * to the caller, and the return value is true. For a successfully handled crash, a RUM
 * Error event is always produced. A RUM View event is ordinarily produced as well, but
 * it may be omitted in cases where the crash belongs to an already-active view that is
 * too old to be updated.
 */
bool ProduceRumEventsForCrash(
    const CrashDump& crash_dump,
    const std::optional<CrashContext>& crash_context,
    const DiagnosticLogger& diagnostic_logger,
    Timestamp current_time,
    const UUID& fallback_application_id,
    std::vector<uint8_t>& encode_buffer,
    const CrashEventSink& sink
);

}  // namespace datadog::impl
