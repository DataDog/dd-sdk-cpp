// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/core/message_bus.hpp"
#include "datadog/impl/diagnostics.hpp"

namespace datadog::impl {

/**
 * Entry point for the messaging thread.
 *
 * Drains `bus._queue` until it is stopped, broadcasting each `FeatureMessage` to every
 * handler in `bus._handlers` in registration order. Because the handler list is fixed
 * at construction time and never mutated, no additional synchronization is needed
 * inside this function.
 */
void MessagingThreadMain(const DiagnosticLogger& diagnostic_logger, MessageBus& bus);

}  // namespace datadog::impl
