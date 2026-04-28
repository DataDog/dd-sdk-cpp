// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <variant>

#include "datadog/attribute.hpp"

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_types/crash_reporting.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"

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
 * Emitted by `Rum` whenever essential session state changes, such as when a new session
 * is created, the active session is stopped, or the `has_tracked_any_view` flag changes
 * state in response to a new view being recorded.
 *
 * Used by `CrashReporting` to persist the latest RUM session state alongside crash
 * reports.
 */
struct RumSessionStateChangedMessage {
  RumSessionState session_state;
};

/**
 * Emitted by `Rum` each time a view event is generated.
 *
 * Used by `CrashReporting` to persist the latest RUM view event alongside crash
 * reports.
 */
struct RumViewEventGeneratedMessage {
  RumViewEvent view_event;
};

/**
 * Emitted by `Rum` when the active view becomes inactive without immediately being
 * replaced by a new view.
 *
 * While `Rum` ordinarily generates a RUM View event with `is_active == false` when a
 * view is explicitly stopped, it does _not_ send a final view event when a view ends
 * due to session expiration. This message provides a reliable means of detecting when
 * RUM no longer has an active view, even in cases where no RUM View event is generated.
 *
 * Used by `CrashReporting` to clear any data that was persisted in response to
 * `RumViewEventGeneratedMessage`, ensuring that the crash context reflects that RUM no
 * longer has an active view.
 */
struct RumViewResetMessage {};

/**
 * Emitted by `Rum` whenever the set of global RUM attributes changes.
 *
 * Used by `CrashReporting` to persist the latest set of global RUM attributes alongside
 * crash reports.
 */
struct RumGlobalAttributesChangedMessage {
  Attribute attributes;
};

/**
 * Emitted by the `CrashReporting` feature when it processes a crash dump indicating
 * that a previous application process detected a crash.
 */
struct CrashReportProcessedMessage {
  CrashReport crash;
};

/**
 * Discriminated union of all message types that can be dispatched through the
 * `MessageBus`. Add new variants here as additional cross-feature communication needs
 * arise.
 */
using FeatureMessage = std::variant<
    ContextChangedMessage,
    RumSessionStateChangedMessage,
    RumViewEventGeneratedMessage,
    RumViewResetMessage,
    RumGlobalAttributesChangedMessage,
    CrashReportProcessedMessage>;

}  // namespace datadog::impl
