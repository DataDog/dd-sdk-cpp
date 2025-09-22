// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/rum.hpp"

#include <mutex>
#include <shared_mutex>

namespace datadog::impl {

Rum::Rum(const RumConfig& config, const platform::IClock& clock)
    : _clock(clock),
      _global_attributes(8),
      _deps(config),
      _application(_deps),
      _application_snapshot() {}

std::optional<Report> Rum::UploadThread_PrepareReport(
    const HttpContext& context, BatchReader& reader
) {
  // TODO(RUM-11368): Implement processing and upload of events once views exist
  (void)context;
  (void)reader;
  return std::nullopt;
}

void Rum::Start() {
  // Dispatch an SDKInit command, which should kick off initialization of our first
  // session when handled by the application scope
  Dispatch(RumCommand::SDKInit(GetBaseCommandParams()));
};

void Rum::Stop() {
  // On Core shutdown, ensure that any RUM context is purged, so that if the SDK is
  // restarted it won't inherit old state
  if (_scope) {
    _scope->UpdateContext([](CoreContext& ctx) { ctx.rum.reset(); });
  }
}

void Rum::SetAttribute(std::string_view name, const Attribute& value) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.SetObjectProperty(name, value);
}

void Rum::DeleteAttribute(std::string_view name) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.DeleteObjectProperty(name);
}

void Rum::StopSession() {
  // Dispatch a StopSession command, which the session scope should handle
  Dispatch(RumCommand::StopSession(GetBaseCommandParams()));
}

RumCommandParams Rum::GetBaseCommandParams(const ObjectAttribute& attributes) const {
  // Create a shallow copy of the global attributes
  std::shared_lock read_only_lock(_global_attributes_mutex);
  ObjectAttribute global_attributes = _global_attributes;
  read_only_lock.unlock();

  // Read the system clock for our issued_at timestamp
  auto issued_at = _clock.Now();

  return RumCommandParams(issued_at, global_attributes, attributes);
}

void Rum::Dispatch(const RumCommand& command) {
  // TODO(RUM-11368): Commands that generate events will need access to the FeatureScope
  _application.Process(command);

  // After every command, update our RumFeatureContext, which makes current RUM state
  // available to other features within the SDK
  UpdateFeatureContext();
}

void Rum::UpdateFeatureContext() {
  // Build a RumContext value (_application_snapshot) that summarizes the current state
  // of our internal scope tree
  UpdateApplicationSnapshot();

  // Write the relevant values to the global RumFeatureContext, so that other features
  // can enrich their events with the latest RUM context
  if (_scope) {
    const RumFeatureContext rum_ctx = _application_snapshot.ToFeatureContext();
    _scope->UpdateContext([rum_ctx](CoreContext& ctx) { ctx.rum = rum_ctx; });
  }
}

void Rum::UpdateApplicationSnapshot() {
  // If we don't have an active session, let the application scope populate our context,
  // and clear any session- or view-related fields
  auto session_opt = _application.GetActiveSession();
  if (!session_opt) {
    _application.PopulateContext(_application_snapshot);
    _application_snapshot.session_id = UUID::Zero;
    // TODO(RUM-11368): _application_snapshot.active_view_id = UUID::Zero;
    // TODO(RUM-11369): _application_snapshot.active_user_action_id = UUID::Zero;
    return;
  }

  // If we have a session but no views, use the session scope and clear view state
  // TODO(RUM-11368): Populate from the last-active RumViewScope, if it exists, rather
  // than RumSessionScope
  const RumSessionScope& session = *session_opt;
  session.PopulateContext(_application_snapshot);

  // TODO(RUM-11369): Ensure that action is represented in snapshot, if any is active
}

}  // namespace datadog::impl
