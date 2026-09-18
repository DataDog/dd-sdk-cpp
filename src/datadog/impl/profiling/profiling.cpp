// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiling.hpp"

#include <memory>
#include <string>
#include <variant>

#include "datadog/impl/core/feature_message.hpp"

#include "dd-win-rum-private.h"

namespace datadog::impl {

Profiling::Profiling(const ProfilerConfig* config) : _config(config) {}

FeatureId Profiling::GetId() const { return CreateFeatureId("PROF"); }

std::string_view Profiling::GetName() const { return "profiling"; }

bool Profiling::Initialize() {
  if (_config) {
    _profiler_setup = SetupProfiler(_config);
  } else {
    ProfilerConfig defaults{};
    defaults.size = sizeof(ProfilerConfig);
    _profiler_setup = SetupProfiler(&defaults);
  }
  _config = nullptr;
  return _profiler_setup;
}

void Profiling::Start() {
  if (!_profiler_setup) {
    return;
  }

  _profiler_started = StartProfiler();
  if (!_profiler_started) {
    _scope->diagnostic_logger.Warning("Failed to start profiling");
  }
}

void Profiling::Stop() {
  if (_profiler_started) {
    if (!SetRumSessionId(nullptr)) {
      _scope->diagnostic_logger.Warning("Failed to clear profiler RUM session");
    }
    StopProfiler();
    _profiler_started = false;
  }
}

std::optional<Report> Profiling::UploadThread_PrepareReport(
    BatchReader&, HttpRequestBuilder&
) {
  return std::nullopt;
}

std::optional<std::function<void(const FeatureMessage&)>>
Profiling::MakeMessageHandler() {
  const auto weak_self = weak_from_this();
  return [weak_self](const FeatureMessage& message) {
    auto self = std::static_pointer_cast<Profiling>(weak_self.lock());
    if (!self) {
      return;
    }

    if (const auto* session = std::get_if<RumSessionStateChangedMessage>(&message)) {
      self->OnRumSessionChanged(session->session_state);
    } else if (const auto* view = std::get_if<RumActiveViewUpdatedMessage>(&message)) {
      self->OnRumViewChanged(view->view_event);
    } else if (std::holds_alternative<RumActiveViewLostMessage>(message)) {
      self->OnRumViewLost();
    }
  };
}

void Profiling::OnRumSessionChanged(const RumSessionState& session) {
  if (!_profiler_started) {
    return;
  }

  const std::string applicationId = session.application_id.ToString();
  if (!SetRumApplicationId(applicationId.c_str())) {
    _scope->diagnostic_logger.Warning("Failed to update profiler RUM application");
    return;
  }

  if (!session.is_active || !session.is_sampled) {
    if (!SetRumSessionId(nullptr)) {
      _scope->diagnostic_logger.Warning("Failed to clear profiler RUM session");
    }
    return;
  }

  const std::string sessionId = session.session_id.ToString();
  if (!SetRumSessionId(sessionId.c_str())) {
    _scope->diagnostic_logger.Warning("Failed to update profiler RUM session");
  }
}

void Profiling::OnRumViewChanged(const RumViewEvent& view) {
  if (!_profiler_started) {
    return;
  }

  const std::string applicationId = view.application.id.ToString();
  const std::string sessionId = view.session.id.ToString();
  const std::string viewId = view.view.id.ToString();
  const std::string& viewName =
      view.view.name.value.empty() ? view.view.url : view.view.name.value;
  RumViewValues values{viewId.c_str(), viewName.c_str()};

  if (!SetRumApplicationId(applicationId.c_str()) ||
      !SetRumSessionId(sessionId.c_str()) || !SetRumView(&values)) {
    _scope->diagnostic_logger.Warning("Failed to update profiler RUM view");
  }
}

void Profiling::OnRumViewLost() {
  if (_profiler_started && !SetRumView(nullptr)) {
    _scope->diagnostic_logger.Warning("Failed to clear profiler RUM view");
  }
}

}  // namespace datadog::impl
