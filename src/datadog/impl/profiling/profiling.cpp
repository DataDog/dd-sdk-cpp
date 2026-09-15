// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiling.hpp"

#include <memory>
#include <utility>
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
    ClearProfilerRumContext();
    StopProfiler();
    _profiler_started = false;
  }
  _application_id_string.clear();
}

std::optional<Report>
Profiling::UploadThread_PrepareReport(BatchReader&, HttpRequestBuilder&) {
  return std::nullopt;
}

std::optional<std::function<void(const FeatureMessage&)>> Profiling::MakeMessageHandler(
) {
  const auto weak_self = weak_from_this();
  return [weak_self](const FeatureMessage& message) {
    const auto* context_changed =
        std::get_if<RumCorrelationContextChangedMessage>(&message);
    if (!context_changed) {
      return;
    }

    auto self = std::static_pointer_cast<Profiling>(weak_self.lock());
    if (!self) {
      return;
    }
    self->OnRumContextChanged(context_changed->context);
  };
}

void Profiling::OnRumContextChanged(const RumCorrelationContext& context) {
  if (!_profiler_started) {
    return;
  }

  std::string application_id = context.application_id.ToString();
  std::string session_id;
  if (context.session_id != UUID::Zero) {
    session_id = context.session_id.ToString();
  }
  std::string view_id;
  if (context.view_id != UUID::Zero) {
    view_id = context.view_id.ToString();
  }

  ::ProfilerRumCorrelationContext profiler_context{};
  profiler_context.application_id = application_id.c_str();
  profiler_context.session_id = session_id.c_str();
  profiler_context.view_id = view_id.c_str();
  profiler_context.view_name = context.view_name.c_str();

  if (!SetRumCorrelationContext(&profiler_context)) {
    _scope->diagnostic_logger.Warning("Failed to update profiler RUM context");
    return;
  }
  _application_id_string = std::move(application_id);
}

void Profiling::ClearProfilerRumContext() {
  if (_application_id_string.empty()) {
    return;
  }

  ::ProfilerRumCorrelationContext profiler_context{};
  profiler_context.application_id = _application_id_string.c_str();
  profiler_context.session_id = "";
  profiler_context.view_id = "";
  profiler_context.view_name = "";
  if (!SetRumCorrelationContext(&profiler_context)) {
    _scope->diagnostic_logger.Warning("Failed to clear profiler RUM context");
  }
}

}  // namespace datadog::impl
