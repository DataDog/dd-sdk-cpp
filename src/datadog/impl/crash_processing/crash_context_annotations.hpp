// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <map>
#include <string>

#include "datadog/impl/types/crash_reporting.hpp"

namespace datadog::impl {

/**
 * Parses a CrashContext from Crashpad annotation parameters. Annotations are expected
 * to be the same set written by crash_handler_crashpad.cpp:
 *
 * - dd.tracking_consent  <JSON string: "granted" | "not-granted" | "pending">
 * - dd.config            <JSON object serialized from ConfigAnnotation>
 * - dd.os                <JSON object serialized from OsAnnotation>
 * - dd.device            <JSON object serialized from DeviceAnnotation>
 * - dd.usr               <JSON object serialized from UsrAnnotation>
 * - dd.account           <JSON object serialized from AccountAnnotation>
 * - dd.rum.config        <JSON object serialized from RumConfigAnnotation>
 * - dd.rum.session       <JSON object serialized from RumSessionAnnotation>
 * - dd.rum.attributes    <JSON object serialized from Attribute::Object()>
 * - dd.rum.last_view     <JSON object serialized from RumViewEvent>
 *                         -or- <empty string, {}, or null if no view was active>
 *
 * All fields are optional: if an annotation is absent or malformed, the corresponding
 * CrashContext fields are left at their zero/empty/default values. It is the caller's
 * responsibility to validate required fields before acting on the result.
 */
CrashContext ParseCrashContextFromAnnotations(
    const std::map<std::string, std::string>& params
);

/**
 * Given a set of parameter values parsed from Crashpad annotations, unconditionally
 * removes the subset of values associated with 'dd.*' keys used to encode CrashContext.
 * The set of keys / annotation names considered for removal is guaranteed to match the
 * same set of keys outlined above for ParseCrashContextFromAnnotations().
 *
 * These annotation values are used exclusively as an IPC mechanism, to share data
 * between the crashing process and the Crashpad handler executable. They need not be
 * present in the Crashpad POST request, so the handler is expected to remove them after
 * parsing their values into a CrashContext, regardless of whether parsing succeeded.
 */
void RemoveCrashContextAnnotations(std::map<std::string, std::string>& params);

}  // namespace datadog::impl
