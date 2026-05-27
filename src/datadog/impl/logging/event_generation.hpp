// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <vector>

#include "datadog/impl/core/feature_scope.hpp"

namespace datadog::impl {

struct LoggerConfigDetails;
struct LogCallDetails;
struct CoreContext;

/**
 * Encapsulates the context-thread logic required to generate a LogEvent in response to
 * an application-initiated log call.
 *
 * `logger` and `call` describe the Logger that was used to initiate the log call, and
 * the call itself (i.e. the details of the message), respectively.
 *
 * `call` is taken by value as a sink parameter: callers are expected to `std::move`
 * into this function, allowing the message string to be moved (not copied) into the
 * resulting `LogEvent`.
 *
 * `encode_buf` is a reusable buffer that can be used to serialize event payloads to
 * JSON, exclusively reserved for the duration of the call.
 */
void ContextThread_GenerateLogEvent(
    const LoggerConfigDetails& logger,
    LogCallDetails call,
    const CoreContext& ctx,
    const EventWriter& event_writer,
    std::vector<uint8_t>& encode_buf
);

}  // namespace datadog::impl
