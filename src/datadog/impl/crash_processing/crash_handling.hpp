// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

class Rum;
struct CrashReport;

/**
 * Runs on the context thread (serially with all other RUM work) when `Rum` handles a
 * `CrashReportProcessedMessage`.
 *
 * If the crash can't or shouldn't be handled, does nothing. Otherwise, attempts to
 * produce a RUM Error event to describes the crash. If a View needs to be synthesized
 * to contain the Error, or if the crash occurred during an existing View that needs to
 * be updated, may also produce a RUM View event preceding the Error.
 *
 * Note that crash reports deal entirely with RUM state established in a prior process
 * that crashed: handling a crash report has no effect whatsoever on the current
 * process's RUM application state as reflected in the scope tree, and therefore does
 * not involve dispatching or processing RumCommands.
 *
 * @param fallback_application_id Currently-configured RUM Application ID for this SDK
 *  instance; used as a fallback in case no application ID is present in CrashContext.
 * @param diagnostic_logger Logger used for debug/warning messages
 * @param current_time Current system time in nanoseconds. This value is only used for
 *  view age threshold checks; it does not require precise synchronization.
 */
void ContextThread_HandleCrashReport(
    const UUID& fallback_application_id,
    const DiagnosticLogger& diagnostic_logger,
    Timestamp current_time,
    RumScopeDependencies& deps,
    const CrashReport& crash,
    const EventWriter& event_writer
);

}  // namespace datadog::impl
