// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <vector>

#include "core/storage_queue.hpp"

namespace datadog::impl {

/**
 * Entry point for the storage thread. See description in `core.hpp`.
 *
 * @param queue Non-owning reference to the thread-safe queue that we should read from;
 *  guaranteed to outlive the thread.
 * @param features Non-owning reference to the array of features that may produce to
 *  that queue. All RegisteredFeature objects contained in the vector are guaranteed to
 *  outlive the thread, and both the objects and the vector itself are guaranteed to
 *  remain immutable for the lifetime of the thread.
 */
void StorageThreadMain(
    StorageQueue& queue, std::vector<struct RegisteredFeature>& features
);

}  // namespace datadog::impl
