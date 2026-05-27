// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <variant>

#include "datadog/attribute.hpp"
#include "datadog/timestamp.hpp"

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
 * Emitted by `Rum` each time the state of the active view changes. Carries the
 * `RumViewEvent` that was generated, to ensure that downstream consumers have a
 * complete, up-to-date description of the active view.
 *
 * `Rum` will only produce this message after generating an event where
 * `view.is_active == true`. Updates to views that are no longer active (but are still
 * kept alive to track pending resources) will not trigger this message. Likewise, when
 * a view is stopped and sends a final event with `view.is_active == false`, no message
 * will be produced. To track when RUM no longer has an active view, use
 * `RumActiveViewLostMessage`.
 *
 * Used by `CrashReporting` to persist the latest RUM view event alongside crash
 * reports.
 */
struct RumActiveViewUpdatedMessage {
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
 * `RumActiveViewUpdatedMessage`, ensuring that the crash context reflects that RUM no
 * longer has an active view.
 */
struct RumActiveViewLostMessage {};

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
 * Emitted by the `Logging` feature when a logger emits an event at `error` or
 * `critical` level, after the log event itself has been written to storage. Carries the
 * log emission timestamp (device time; RUM applies the server-time offset), the log
 * message, and the merged set of user attributes (global logging attrs + logger attrs +
 * per-call attrs). `source` is always `"logger"` and is hard-coded by the RUM receiver.
 *
 * Session, view, and action IDs are NOT included — RUM's receiver resolves them from
 * its own current view scope at message-receive time.
 */
struct LoggerErrorMessage {
  Timestamp timestamp;
  std::string message;
  Attribute attributes;
};

/**
 * Discriminated union of all message types that can be dispatched through the
 * `MessageBus`. Add new variants here as additional cross-feature communication needs
 * arise.
 */
using FeatureMessage = std::variant<
    ContextChangedMessage,
    RumSessionStateChangedMessage,
    RumActiveViewUpdatedMessage,
    RumActiveViewLostMessage,
    RumGlobalAttributesChangedMessage,
    CrashReportProcessedMessage,
    LoggerErrorMessage>;

}  // namespace datadog::impl
