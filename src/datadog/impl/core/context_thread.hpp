// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Entry point for the context thread.
 *
 * The context thread continually reads functions from the context queue, execute each
 * function serially as it's consumed. This allows features to perform operations
 * asynchronously without blocking their callers.
 *
 * Functions are enqueued via FeatureScope::ExecuteOnContextThread et al. - FeatureScope
 * is responsible for wrapping Feature-provided functions in a thunk that will evaluate
 * the required parameters (CoreContext and EventWriter) from a CoreContextProvider, so
 * the context thread itself is dead-simple: it just executes those thunks.
 *
 * @param diagnostic_logger Interface for logging status/warning messages.
 * @param queue Non-owning reference to the thread-safe queue that we read from;
 *  guaranteed to outlive the thread.
 */
void ContextThreadMain(
    const DiagnosticLogger& diagnostic_logger, Queue<std::function<void()>>& queue
);

}  // namespace datadog::impl
