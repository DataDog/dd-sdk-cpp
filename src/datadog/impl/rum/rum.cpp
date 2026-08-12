// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/rum.hpp"

#include <mutex>
#include <shared_mutex>
#include <string_view>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/http/body_writer_tlv.hpp"
#include "datadog/impl/core/http/request_builder.hpp"
#include "datadog/impl/rum/crash_processing/crash_handling.hpp"
#include "datadog/impl/rum/resource_types.hpp"

namespace datadog::impl {
Rum::Rum(const RumConfig& config, const platform::IClock& clock)
    : _global_attributes(8),
      _deps(config, clock),
      _application(_deps),
      _application_snapshot() {}

std::optional<Report> Rum::UploadThread_PrepareReport(
    BatchReader& reader, HttpRequestBuilder& builder
) {
  // This preliminary implementation just streams all RUM events directly, a la Logging

  // TODO(RUM-12546): Implement filtering/deduplication of view events, unless we
  // implement `view_update` events and find that make filtering unnecessary

  // TODO(RUM-12242): If necessary to prevent the creation of microsessions on the
  // backend, filter out events for synthetic views created on launch

  // Data is sent to RUM intake, as a JSON-serialized array of various RUM event payload
  // types (e.g. RumViewEvent, RumResourceEvent, etc.)
  builder.Reset("/api/v2/rum", "application/json");
  builder.AddQueryParam_ddsource();

  // Prepare a TLVBatchWriter which will stream the contents of the batch file, treating
  // each event block as a JSON object and concatenating those values into a JSON array
  // on the fly as the request body is built
  return Report{builder.GetUrl(), builder.GetHeaders(), TLVBatchWriter{reader}};
}

std::optional<std::function<void(const FeatureMessage&)>> Rum::MakeMessageHandler() {
  // Bind a weak_ptr to this so the callback will silently no-op after we're destroyed
  const auto weak_self = weak_from_this();
  return [weak_self](const FeatureMessage& msg) {
  // MSVC flags each `else if (const auto* m = ...)` branch as shadowing an outer `m`,
  // even though the scopes are mutually exclusive
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4456)
#endif
    // On CrashReportProcessedMessage, enqueue a context-thread callback that will
    // process a copy of the CrashReport, potentially producing a RUM Error event to
    // describe the crash, and also potentially producing an updated RUM View event
    if (const auto* m = std::get_if<CrashReportProcessedMessage>(&msg)) {
      // Abort if our weak_ptr is no longer valid
      auto self = std::static_pointer_cast<Rum>(weak_self.lock());
      if (!self) {
        return;
      }

      // Get off the messaging thread ASAP: copy the CrashReport value to the context
      // thread for processing
      self->_scope->ExecuteOnContextThread([weak_self, crash = m->crash](
                                               const CoreContext&,
                                               const EventWriter& event_writer,
                                               const MessagePublisher&
                                           ) {
        // We're now on the context thread; check that our Rum implementation is
        // still alive
        auto self = std::static_pointer_cast<Rum>(weak_self.lock());
        if (!self) {
          return;
        }
        ContextThread_HandleCrashReport(self->_deps, crash, event_writer);
      });
    } else if (const auto* m = std::get_if<LogErrorGeneratedMessage>(&msg)) {
      // Abort if our weak_ptr is no longer valid
      auto self = std::static_pointer_cast<Rum>(weak_self.lock());
      if (!self) {
        return;
      }

      // We'll generate an AddError command to record a RUM Error, but we need it to
      // carry basic information from our log event:
      // - attributes is the result of merging our log message's custom attributes into
      //   the set of
      // - issued_at reflects the local system time at the moment that the call to
      //   Logger::Error(), Logger::Critical() etc. was made
      RumCommandParams params = self->GetBaseCommandParams(m->attributes);
      params.issued_at = m->timestamp;

      // Dispatch an AddError command that records the error details, with the
      // 'error.source' value hardcoded to "logger"
      self->DispatchAsync(
          RumCommand::AddError(
              std::move(params),
              RumErrorSource::Logger,
              RumErrorDetails{m->error_message, m->error_kind, m->error_stack}
          )
      );
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  };
}

void Rum::Start() {
  // _scope is initialized on SDK start; it's always valid here
  if (!_scope) {
    DATADOG_ASSERT(false, "_scope is invalid on Rum::Start");
    return;
  }

  // Propagate our FeatureScope's diagnostic logger into _deps before reinitializing
  // the scope tree, creating a copy since _scope is reset on Stop()
  _deps.diagnostic_logger = _scope->diagnostic_logger;

  // Expose (or hide) CoreContext::anonymous_id on RUM/Log events, per our config's
  // trackAnonymousUser setting. The value itself was already resolved by Core::Init();
  // we're just flipping the gate that both RUM's and Logging's event enrichment check.
  _scope->UpdateContext([enabled = _deps.track_anonymous_user](CoreContext& ctx) {
    ctx.anonymous_id_enabled = enabled;
  });

  // Fully reinitialize RUM application state to clear all sessions/views/etc. from
  // previous runs
  _application = RumApplicationScope(_deps);

  // Take a snapshot of our global attributes, and enqueue a
  // RumGlobalAttributesChangedMessage to ensure that downstream features have a
  // consistent view of any attribute changes that occurred while the SDK wasn't running
  Attribute snapshot;
  {
    std::shared_lock lock(_global_attributes_mutex);
    snapshot = _global_attributes.attribute;
  }
  _scope->ExecuteOnContextThread(
      [snapshot = std::move(snapshot)](
          const CoreContext&, const EventWriter&, const MessagePublisher& pub
      ) mutable { pub(RumGlobalAttributesChangedMessage{std::move(snapshot)}); }
  );

  // Dispatch SDKInit to start first session
  DispatchAsync(RumCommand::SDKInit(GetBaseCommandParams()));
};

void Rum::Stop() {
  // Reset the once-per-SDK-lifetime TTID guard so that it fires correctly if the SDK
  // is stopped and restarted (e.g. in tests).
  _ttid_reported.store(false, std::memory_order_relaxed);

  // Reset the once-per-SDK-lifetime TTFD guard so that it fires correctly if the SDK
  // is stopped and restarted (e.g. in tests).
  _ttfd_reported.store(false, std::memory_order_relaxed);

  // Note: _application will be destroyed when Rum is destroyed (after Core joins
  // the context thread). In-flight lambdas can safely access _application as long as
  // weak_ptr.lock() succeeds. CoreContext is reset by Core::Start() at the top of each
  // new run, so no context mutation is needed here.
}

void Rum::AddAttribute(std::string_view name, const Attribute& value) {
  // Acquire an exclusive lock on the set of global RUM attributes, then modify the
  // attribute object and capture a snapshot of the resulting state
  Attribute snapshot;
  {
    std::unique_lock lock(_global_attributes_mutex);
    _global_attributes.attribute.SetObjectProperty(name, value);
    snapshot = _global_attributes.attribute;
  }

  // If we have no valid FeatureScope, the SDK is not yet running
  if (!_scope) {
    return;
  }
  FeatureScope& scope = *_scope;

  // Otherwise, enqueue a context-thread callback that will publish a message conveying
  // the latest snapshot of our set of global RUM attributes, using `mutable` to ensure
  // that the snapshot value can be moved into the message
  scope.ExecuteOnContextThread(
      [snapshot = std::move(snapshot)](
          const CoreContext&, const EventWriter&, const MessagePublisher& pub
      ) mutable { pub(RumGlobalAttributesChangedMessage{std::move(snapshot)}); }
  );
}

void Rum::RemoveAttribute(std::string_view name) {
  // Acquire an exclusive lock on the set of global RUM attributes, then modify the
  // attribute object and capture a snapshot of the resulting state
  Attribute snapshot;
  {
    std::unique_lock lock(_global_attributes_mutex);
    _global_attributes.attribute.DeleteObjectProperty(name);
    snapshot = _global_attributes.attribute;
  }

  // If we have no valid FeatureScope, the SDK is not yet running
  if (!_scope) {
    return;
  }
  FeatureScope& scope = *_scope;

  // SDK is running: publish a message with our new snapshot of global RUM attributes,
  // moving the value into the message
  scope.ExecuteOnContextThread(
      [snapshot = std::move(snapshot)](
          const CoreContext&, const EventWriter&, const MessagePublisher& pub
      ) mutable { pub(RumGlobalAttributesChangedMessage{std::move(snapshot)}); }
  );
}

void Rum::StopSession() {
  // Dispatch a StopSession command, which the session scope should handle
  DispatchAsync(RumCommand::StopSession(GetBaseCommandParams()));
}

void Rum::StartView(
    std::string_view key, std::string_view name, const Attribute& attributes
) {
  // Dispatch a StartView command to be handled by the active session
  DispatchAsync(RumCommand::StartView(GetBaseCommandParams(attributes), key, name));
}

void Rum::AddViewAttribute(std::string_view name, const Attribute& value) {
  // TODO(RUM-11363): Log a warning if there's no active view to receive the command?
  DispatchAsync(RumCommand::AddViewAttribute(GetBaseCommandParams(), name, value));
}

void Rum::RemoveViewAttribute(std::string_view name) {
  DispatchAsync(RumCommand::RemoveViewAttribute(GetBaseCommandParams(), name));
}

void Rum::StopView(std::string_view key, const Attribute& attributes) {
  // Dispatch a StopView command
  DispatchAsync(RumCommand::StopView(GetBaseCommandParams(attributes), key));
}

void Rum::AddAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  DispatchAsync(RumCommand::AddAction(GetBaseCommandParams(attributes), type, name));
}

void Rum::StartAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  DispatchAsync(RumCommand::StartAction(GetBaseCommandParams(attributes), type, name));
}

void Rum::StopAction(std::string_view new_name, const Attribute& attributes) {
  DispatchAsync(RumCommand::StopAction(GetBaseCommandParams(attributes), new_name));
}

void Rum::StartResource(
    std::string_view key, const RumRequestDetails& request, const Attribute& attributes
) {
  DispatchAsync(
      RumCommand::StartResource(GetBaseCommandParams(attributes), key, request)
  );
}

void Rum::StopResource(
    std::string_view key,
    RumResponseDetails response,
    const std::optional<RumErrorDetails>& error,
    const Attribute& attributes
) {
  DispatchAsync(
      RumCommand::StopResource(GetBaseCommandParams(attributes), key, response, error)
  );
}

void Rum::AddError(
    RumErrorSource source, const RumErrorDetails& error, const Attribute& attributes
) {
  DispatchAsync(RumCommand::AddError(GetBaseCommandParams(attributes), source, error));
}

void Rum::AddLongTask(Duration duration, const Attribute& attributes) {
  DispatchAsync(RumCommand::AddLongTask(GetBaseCommandParams(attributes), duration));
}

void Rum::StartOperation(
    std::string_view name,
    std::optional<std::string_view> operation_key,
    const Attribute& attributes
) {
  DispatchAsync(
      RumCommand::StartOperation(GetBaseCommandParams(attributes), name, operation_key)
  );
}

void Rum::StopOperation(
    std::string_view name,
    std::optional<std::string_view> operation_key,
    std::optional<RumOperationFailureReason> failure_reason,
    const Attribute& attributes
) {
  DispatchAsync(
      RumCommand::StopOperation(
          GetBaseCommandParams(attributes), name, operation_key, failure_reason
      )
  );
}

void Rum::ReportAppDisplayInitialized() {
  // If the SDK is not running, drop silently — the same behaviour as every other
  // DispatchAsync call when _scope is unset. Critically, do this check *before*
  // consuming the one-shot _ttid_reported guard so that a pre-Start() or
  // post-Stop() call does not permanently burn the guard and cause a subsequent
  // valid call (after Start()) to be rejected as a duplicate.
  if (!_scope) {
    return;
  }

  // Only emit once per SDK lifetime. Use test-and-set to guard against races between
  // concurrent callers without holding any lock: the first caller wins, all others
  // receive a warning and are dropped.
  bool expected = false;
  if (!_ttid_reported.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel, std::memory_order_acquire
      )) {
    _deps.diagnostic_logger.Warning(
        "Rum::ReportAppDisplayInitialized call ignored: already called once for this "
        "SDK instance"
    );
    return;
  }

  DispatchAsync(RumCommand::ReportAppDisplayInitialized(GetBaseCommandParams()));
}

void Rum::ReportAppFullyDisplayed() {
  // If the SDK is not running, drop silently - the same behavior as every other
  // DispatchAsync call when _scope is unset. Critically, do this check *before*
  // consuming the one-shot _ttfd_reported guard so that a pre-Start() or
  // post-Stop() call does not permanently burn the guard and cause a subsequent
  // valid call (after Start()) to be rejected as a duplicate.
  if (!_scope) {
    return;
  }

  // Only emit once per SDK lifetime. Use test-and-set to guard against races between
  // concurrent callers without holding any lock: the first caller wins, all others
  // receive a warning and are dropped.
  bool expected = false;
  if (!_ttfd_reported.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel, std::memory_order_acquire
      )) {
    _deps.diagnostic_logger.Warning(
        "Rum::ReportAppFullyDisplayed call ignored: already called once for this "
        "SDK instance"
    );
    return;
  }

  DispatchAsync(RumCommand::ReportAppFullyDisplayed(GetBaseCommandParams()));
}

RumCommandParams Rum::GetBaseCommandParams(const Attribute& attributes) const {
  // Create a shallow copy of the global attributes
  std::shared_lock read_only_lock(_global_attributes_mutex);
  Attribute global_attributes = _global_attributes.attribute;
  read_only_lock.unlock();

  // Read the system clock for our issued_at timestamp
  auto issued_at = _deps.clock.Now();

  return RumCommandParams(issued_at, global_attributes, attributes);
}

void Rum::DispatchAsync(const RumCommand& command) {
  // If our _scope is no longer valid, the SDK has been stopped prior to this call
  if (!_scope) {
    return;
  }
  FeatureScope& scope = *_scope;

  // Capture weak_ptr to self for fail-safe shutdown detection
  auto weak_rum = std::weak_ptr<Rum>(std::static_pointer_cast<Rum>(shared_from_this()));

  // Enqueue a function to run on the context thread, capturing the command being
  // dispatched: when this function is executed, the context thread will process the
  // command, updating internal RUM state and potentially producing events
  scope.ExecuteOnContextThread([weak_rum, cmd = command](
                                   const CoreContext& context,
                                   const EventWriter& writer,
                                   const MessagePublisher& publisher
                               ) {
    auto rum = weak_rum.lock();
    if (!rum) {
      return;
    }
    rum->_application.Process(cmd, context, writer);
    rum->UpdateApplicationSnapshot();
    rum->BroadcastStateChanges(publisher);
  });

  // Enqueue a context-mutation function that will run immediately following the
  // processing of the command
  scope.UpdateContext([weak_rum](CoreContext& ctx) {
    if (auto rum = weak_rum.lock()) {
      // _application_snapshot has been updated with the result of processing our
      // command; write the relevant UUIDs to the global RumFeatureContext, so that
      // other features can enrich their events with RUM data
      ctx.rum = rum->_application_snapshot.ToFeatureContext();
    }
  });
}

void Rum::UpdateApplicationSnapshot() {
  // Reset our RumContext value to zero, without releasing string buffers etc.
  _application_snapshot.Reset();

  // If we don't have an active session, let the application scope populate our context
  auto session_opt = _application.GetActiveSession();
  if (!session_opt) {
    _application.PopulateContext(_application_snapshot);
    return;
  }

  // If we have a session but no views, populate from session scope
  const RumSessionScope& session = *session_opt;
  auto view_opt = session.GetActiveView();
  if (!view_opt) {
    session.PopulateContext(_application_snapshot);
    return;
  }

  // If our active session has an active view, but that view has no actions, populate
  // from view scope
  const RumViewScope& view = *view_opt;
  auto action_opt = view.GetActiveAction();
  if (!action_opt) {
    view.PopulateContext(_application_snapshot);
    return;
  }

  // If we have an active session, view, and action, populate from action scope
  const RumActionScope& action = *action_opt;
  action.PopulateContext(_application_snapshot);
}

void Rum::BroadcastStateChanges(const MessagePublisher& publisher) {
  // If any RUM View events were generated during processing of the most recent command,
  // publish a RumActiveViewUpdatedMessage containing the most-recently-produced event
  if (auto ev = _application.ConsumeLastActiveViewEvent()) {
    publisher(RumActiveViewUpdatedMessage{std::move(*ev)});
  }

  // If the active view ID is now UUID::Zero, and it was nonzero the last time we
  // processed a command, then we no longer have an active view: publish a
  // RumActiveViewLostMessage so downstream features can clear their last-view-event
  // state
  const UUID current_view_id = _application_snapshot.active_view_id;
  if (_last_broadcast_view_id != UUID::Zero && current_view_id == UUID::Zero) {
    publisher(RumActiveViewLostMessage{});
  }
  _last_broadcast_view_id = current_view_id;

  // Capture a RumSessionState snapshot describing the essential details of our
  // currently-active view (or most-recently-active view, if stopped), and determine if
  // session state has meaningfully changed since the last time we processed a command
  auto state = _application.GetCurrentSessionState();
  if (state.has_value() != _last_broadcast_session_state.has_value() ||
      (state && *state != *_last_broadcast_session_state)) {
    // If so, and if we have valid session state, publish a RumSessionStateChanged
    // message
    if (state) {
      publisher(RumSessionStateChangedMessage{*state});
    }
    _last_broadcast_session_state = state;
  }
}

}  // namespace datadog::impl
