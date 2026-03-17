// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/profiling/profiling.hpp"

#include <string>

#include "datadog/uuid.hpp"

namespace datadog::impl {

Profiling::Profiling(const ProfilerConfig* config) {
  // Call SetupProfiler immediately so dd-win-prof interns all string fields.
  // This avoids needing to deep-copy the config's const char* pointers.
  if (config != nullptr) {
    _profiler_setup = SetupProfiler(config);
  } else {
    ProfilerConfig defaults{};
    defaults.size = sizeof(ProfilerConfig);
    _profiler_setup = SetupProfiler(&defaults);
  }
}

FeatureId Profiling::GetId() const { return CreateFeatureId("PROF"); }

std::string_view Profiling::GetName() const { return "profiling"; }

void Profiling::Start() {
  if (_profiler_setup) {
    StartProfiler();
  }
}

void Profiling::Stop() {
  // Don't clear _profiler_setup — StopProfiler/StartProfiler can be called
  // repeatedly, and Core supports stop/restart cycles.
  if (_profiler_setup) {
    StopProfiler();
  }
}

std::optional<Report> Profiling::UploadThread_PrepareReport(
    const HttpContext& /*context*/, BatchReader& /*reader*/
) {
  // dd-win-prof manages its own upload pipeline via libdatadog.
  return std::nullopt;
}

// TODO: RumContextSnapshot passes UUIDs which we convert to strings here. If the
// copy cost matters, RUM could own pre-formatted string representations and pass
// const char* pointers directly, avoiding the UUID→string conversion on this side.
void Profiling::OnRumContextChanged(const datadog::RumContextSnapshot& context) {
  if (!_profiler_setup) {
    return;
  }

  // Session end: all UUIDs are zero — clear everything and return
  if (context.application_id == datadog::UUID::Zero) {
    _prev_application_id = datadog::UUID::Zero;
    _prev_session_id = datadog::UUID::Zero;
    _prev_view_id = datadog::UUID::Zero;
    ClearRumContext();
    return;
  }

  // Stable context: only call SetRumSession when application_id or session_id changes
  if (context.application_id != _prev_application_id ||
      context.session_id != _prev_session_id) {
    _prev_application_id = context.application_id;
    _prev_session_id = context.session_id;

    _app_id_str = context.application_id.ToString();
    _session_id_str = context.session_id.ToString();

    RumSessionContext session_ctx{};
    session_ctx.application_id = _app_id_str.c_str();
    session_ctx.session_id = _session_id_str.c_str();
    SetRumSession(&session_ctx);
  }

  // Volatile context: only call SetRumView when view_id changes
  if (context.view_id != _prev_view_id) {
    _prev_view_id = context.view_id;

    if (context.view_id == datadog::UUID::Zero) {
      SetRumView(nullptr);
    } else {
      _view_id_str = context.view_id.ToString();

      RumViewValues view_vals{};
      view_vals.view_id = _view_id_str.c_str();
      view_vals.view_name = context.view_name;
      SetRumView(&view_vals);
    }
  }
}

}  // namespace datadog::impl
