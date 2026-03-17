// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/profiling/profiling.hpp"

#include <cstring>
#include <string>

#include "datadog/uuid.hpp"

namespace datadog::impl {

// Helper: copy a const char* into owned storage, return pointer to owned string
// (or nullptr if source is null/empty)
static const char* CopyString(std::string& storage, const char* src) {
  if (src == nullptr || src[0] == '\0') {
    storage.clear();
    return nullptr;
  }
  storage = src;
  return storage.c_str();
}

Profiling::Profiling(const ProfilerConfig* config) {
  if (config != nullptr) {
    _has_config = true;

    // Deep-copy the struct, then repoint const char* fields at our owned strings
    _config = *config;

    _config.url = CopyString(_url, config->url);
    _config.apiKey = CopyString(_api_key, config->apiKey);
    _config.serviceEnvironment =
        CopyString(_service_environment, config->serviceEnvironment);
    _config.serviceName = CopyString(_service_name, config->serviceName);
    _config.serviceVersion = CopyString(_service_version, config->serviceVersion);
    _config.tags = CopyString(_tags, config->tags);
    _config.pprofOutputDirectory =
        CopyString(_pprof_output_directory, config->pprofOutputDirectory);
  }
}

FeatureId Profiling::GetId() const { return CreateFeatureId("PROF"); }

std::string_view Profiling::GetName() const { return "profiling"; }

void Profiling::Start() {
  if (_has_config) {
    _profiler_setup = SetupProfiler(&_config);
  } else {
    // No config provided — set up with defaults (size field required)
    ProfilerConfig defaults{};
    std::memset(&defaults, 0, sizeof(defaults));
    defaults.size = sizeof(ProfilerConfig);
    _profiler_setup = SetupProfiler(&defaults);
  }

  if (_profiler_setup) {
    StartProfiler();
  }
}

void Profiling::Stop() {
  if (_profiler_setup) {
    StopProfiler();
    _profiler_setup = false;
  }
}

std::optional<Report> Profiling::UploadThread_PrepareReport(
    const HttpContext& /*context*/, BatchReader& /*reader*/
) {
  // dd-win-prof manages its own upload pipeline via libdatadog.
  return std::nullopt;
}

void Profiling::OnRumContextChanged(const datadog::RumContextSnapshot& context) {
  if (!_profiler_setup) {
    return;
  }

  // Stable context: application_id and session_id
  const std::string app_id = context.application_id.ToString();
  const std::string session_id = context.session_id.ToString();

  RumSessionContext session_ctx{};
  session_ctx.application_id = app_id.c_str();
  session_ctx.session_id = session_id.c_str();
  SetRumSession(&session_ctx);

  // Volatile context: view_id and view_name
  if (context.view_id == datadog::UUID::Zero) {
    SetRumView(nullptr);
  } else {
    const std::string view_id = context.view_id.ToString();
    RumViewValues view_vals{};
    view_vals.view_id = view_id.c_str();
    view_vals.view_name = context.view_name;
    SetRumView(&view_vals);
  }
}

}  // namespace datadog::impl
