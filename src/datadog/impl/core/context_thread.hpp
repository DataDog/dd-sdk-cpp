// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/diagnostics.hpp"

namespace datadog::impl {

/**
 * Entry point for the context thread.
 *
 * The context thread processes closures submitted by features, providing each
 * closure with a snapshot of the CoreContext at execution time. This allows
 * features to perform operations asynchronously without blocking their callers.
 *
 * The thread runs until the queue is stopped and drained, processing closures in
 * FIFO order.
 *
 * @param diagnostic_logger Interface for logging status/warning messages.
 * @param queue Non-owning reference to the thread-safe queue that we read from;
 *  guaranteed to outlive the thread.
 * @param context_provider Non-owning reference to the CoreContextProvider that
 *  supplies context snapshots; guaranteed to outlive the thread.
 */
void ContextThreadMain(
    const DiagnosticLogger& diagnostic_logger,
    Queue<std::function<void()>>& queue,
    CoreContextProvider& context_provider
);

}  // namespace datadog::impl
