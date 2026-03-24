// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <variant>

#include "datadog/impl/core/context.hpp"

namespace datadog::impl {

/**
 * Carries a snapshot of `CoreContext` captured immediately after a
 * `CoreContextProvider::Update()` call completes. Subscribers can use this snapshot to
 * react proactively to state changes in other features — the primary use case being
 * `CrashReporting`, which needs to persist RUM session/view/action IDs every time
 * `RumFeatureContext` changes so that those IDs are available in a crash report
 * generated before the next normal launch.
 *
 * The snapshot is taken inside `Update()` while the write lock is still held, ensuring
 * the delivered context is consistent and not partially written.
 */
struct ContextChangedMessage {
  CoreContext context;
};

/**
 * Discriminated union of all message types that can be dispatched through the
 * `MessageBus`. Add new variants here as additional cross-feature communication needs
 * arise.
 */
using FeatureMessage = std::variant<ContextChangedMessage>;

}  // namespace datadog::impl
