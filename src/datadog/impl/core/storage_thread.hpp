// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <vector>

#include "datadog/impl/core/storage_queue.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Handles one item from the storage queue. See description in `core.hpp`.
 *
 * Must only be called serially: the storage queue runs its items one at a time, which
 * makes it the only writer to each feature's BatchWriter.
 *
 * @param diagnostic_logger Interface for logging local status/warning messages.
 * @param features Non-owning reference to the array of features that may produce
 *  storage messages. The vector and its items remain immutable while the storage queue
 *  exists.
 * @param message The message to handle.
 */
void HandleStorageMessage(
    const DiagnosticLogger& diagnostic_logger,
    std::vector<struct RegisteredFeature>& features,
    const StorageMessage& message
);

}  // namespace datadog::impl
