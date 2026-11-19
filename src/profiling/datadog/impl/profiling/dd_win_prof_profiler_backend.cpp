// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// IProfilerBackend implementation backed by dd-win-prof. Only compiled into builds with
// profiling support (see cmake/deps/dd-win-prof.cmake).

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <utility>

// Keep the SDK and dd-win-prof declarations below the system headers.
#include "datadog/impl/profiling/dd_win_prof_profiler_backend.hpp"

namespace datadog::impl {

namespace {

// Whether a DdWinProfBackend exists: dd-win-prof is process-global
std::atomic<bool> s_backend_exists{false};

class DdWinProfBackend final : public IProfilerBackend {
 public:
  DdWinProfBackend(const DiagnosticLogger& logger, DdWinProfApi api)
      : _logger(logger), _api(api) {}

  ~DdWinProfBackend() override {
    Stop();
    s_backend_exists.store(false);
  }

  bool SetProfileCallback(ProfileCallback callback) override {
    if (!callback) {
      return UnregisterCallback();
    }

    _callback = std::move(callback);
    if (!_api.setup_callback(DeliverProfile, this)) {
      _callback = {};
      // Registration failure preserves the profiler's previous registration. This
      // backend never installs another callback, but still request the empty state.
      (void)_api.setup_callback(nullptr, nullptr);
      return false;
    }
    _callback_registered = true;
    return true;
  }

  bool Init(const ProfilerBackendSettings& settings) override {
    ProfilerConfig config = {};
    config.size = sizeof(ProfilerConfig);

    // Leave the endpoint, API key, and tuning parameters unset so that dd-win-prof
    // takes them from its environment variables
    config.noEnvVars = false;
    config.serviceName = settings.service.c_str();
    config.serviceEnvironment = settings.env.c_str();
    // Only pass a version if the application provided one
    config.serviceVersion =
        settings.version.empty() ? nullptr : settings.version.c_str();

    // dd-win-prof copies these strings, so they need only outlive this call
    if (_api.setup(&config)) {
      return true;
    }
    UnregisterCallback();
    return false;
  }

  bool Start() override {
    if (_api.start()) {
      return true;
    }
    // Safe after a failed or partial start, and waits for any callback in progress.
    _api.stop();
    UnregisterCallback();
    return false;
  }

  void Stop() override {
    _api.stop();
    UnregisterCallback();
  }

 private:
  static void DD_WIN_PROF_CALL
  DeliverProfile(const ProfilingData* data, void* user_data) noexcept {
    if (user_data == nullptr) {
      // Missing backend context; unable to deliver the profile.
      return;
    }
    static_cast<DdWinProfBackend*>(user_data)->DeliverProfile(data);
  }

  void DeliverProfile(const ProfilingData* data) noexcept {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
      DeliverProfileImpl(data);
    } catch (const std::bad_alloc&) {
      WarnNoexcept("Could not allocate memory for a profiler callback");
    } catch (const std::exception&) {
      WarnNoexcept("Profiling data callback threw an exception");
    } catch (...) {
      WarnNoexcept("Profiling data callback threw an unknown exception");
    }
#else
    DeliverProfileImpl(data);
#endif
  }

  void DeliverProfileImpl(const ProfilingData* data) {
    // Registration changes happen only while stopped, and StopProfiler waits for
    // callbacks before UnregisterCallback clears this function.
    if (!_callback) {
      // No SDK consumer is registered; unable to deliver the profile.
      return;
    }
    // ProfilingData is append-only: reject missing fields but accept future tails.
    if (data == nullptr || data->struct_size < sizeof(ProfilingData)) {
      WarnNoexcept("Profiler delivered an invalid profiling data view");
      // Invalid view; borrowed profile data cannot be read safely.
      return;
    }
    if (data->result == 0 && data->pprof_size != 0 &&
        (data->pprof_bytes == nullptr ||
         data->pprof_size >
             static_cast<size_t>((std::numeric_limits<std::ptrdiff_t>::max)()))) {
      WarnNoexcept("Profiler delivered an invalid pprof byte view");
      // Invalid byte range; borrowed profile data cannot be copied safely.
      return;
    }

    ProfileData profile;
    if (data->result == 0) {
      if (data->pprof_size != 0) {
        profile.pprof.assign(data->pprof_bytes, data->pprof_bytes + data->pprof_size);
      }
    } else {
      const char* message = _api.error_message(data->result);
      if (message != nullptr) {
        profile.error = message;
      }
    }
    profile.start = Timestamp{std::chrono::milliseconds(data->start_unix_time_ms)};
    profile.end = Timestamp{std::chrono::milliseconds(data->end_unix_time_ms)};
    profile.is_last = data->is_last != 0;

    _callback(std::move(profile));
  }

  bool UnregisterCallback() noexcept {
    if (!_callback_registered) {
      _callback = {};
      return true;
    }
    const bool unregistered = _api.setup_callback(nullptr, nullptr);
    if (!unregistered) {
      WarnNoexcept("Failed to unregister the profiling callback");
    }
    _callback_registered = false;
    _callback = {};
    return unregistered;
  }

  void WarnNoexcept(const char* message) const noexcept {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
      _logger.Warning(message);
    } catch (...) {
      // Diagnostic handlers are application code; no exception may cross the C ABI.
    }
#else
    _logger.Warning(message);
#endif
  }

  DiagnosticLogger _logger;
  DdWinProfApi _api;
  ProfileCallback _callback;
  bool _callback_registered = false;
};

}  // namespace

std::unique_ptr<IProfilerBackend> CreateDdWinProfBackendForTest(
    const DiagnosticLogger& logger, DdWinProfApi api
) {
  return std::make_unique<DdWinProfBackend>(logger, api);
}

std::unique_ptr<IProfilerBackend> CreateProfilerBackend(
    const DiagnosticLogger& logger
) {
  bool expected = false;
  if (!s_backend_exists.compare_exchange_strong(expected, true)) {
    logger.Warning(
        "Profiling is already enabled for another SDK instance in this process: the "
        "profiler is shared by the whole process, so only one instance can use it"
    );
    return nullptr;
  }
  return std::make_unique<DdWinProfBackend>(
      logger,
      DdWinProfApi{
          SetupProfilingDataCallback,
          SetupProfiler,
          StartProfiler,
          StopProfiler,
          GetProfilingErrorMessage
      }
  );
}

}  // namespace datadog::impl
