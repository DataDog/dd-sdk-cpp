// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// IProfilerBackend implementation backed by dd-win-prof. Only compiled into builds with
// profiling support (see cmake/deps/dd-win-prof.cmake).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string_view>
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
      // Registration failed, and dd-win-prof keeps its previous (empty) registration;
      // drop the callback that will never be called
      _callback = {};
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
    ProfileView profile;
    profile.start = Timestamp{std::chrono::milliseconds(data->start_unix_time_ms)};
    profile.end = Timestamp{std::chrono::milliseconds(data->end_unix_time_ms)};
    profile.is_last = data->is_last != 0;

    if (data->result != 0) {
      // dd-win-prof returns static text that's valid until the DLL is unloaded, which
      // happens only at process exit
      const char* message = _api.error_message(data->result);
      profile.error = (message != nullptr && message[0] != '\0')
                          ? std::string_view(message)
                          : std::string_view("Unknown profiler error");
      WarnNoexcept(
          "Profiler failed to export a profile",
          {{"result", static_cast<uint64_t>(data->result)}, {"error", profile.error}}
      );
    } else if (data->pprof_size != 0 && data->pprof_bytes == nullptr) {
      // Invalid byte range; report the period as failed rather than read the bytes.
      static constexpr const char* INVALID_PPROF_VIEW_ERROR =
          "Profiler delivered an invalid pprof byte view";
      profile.error = INVALID_PPROF_VIEW_ERROR;
      WarnNoexcept(INVALID_PPROF_VIEW_ERROR);
    } else if (data->pprof_size != 0) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      const auto* pprof_bytes = reinterpret_cast<const char*>(data->pprof_bytes);
      profile.pprof = Block(pprof_bytes, data->pprof_size);
    }

    _callback(profile);
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

  void WarnNoexcept(
      const char* message, DiagnosticAttributeList attributes = {}
  ) const noexcept {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
      _logger.Warning(message, attributes);
    } catch (...) {
      // Diagnostic handlers are application code; no exception may cross the C ABI.
    }
#else
    _logger.Warning(message, attributes);
#endif
  }

  DiagnosticLogger _logger;
  DdWinProfApi _api;
  ProfileCallback _callback;
  bool _callback_registered = false;
};

}  // namespace

/**
 * Claims the process-wide backend slot, then creates a backend that calls `api`. The
 * backend releases the slot when destroyed. On success, returns the new backend. If
 * another backend already holds the slot, logs a warning and returns null.
 */
static std::unique_ptr<IProfilerBackend> create_backend(
    const DiagnosticLogger& logger, DdWinProfApi api
) {
  bool expected = false;
  if (!s_backend_exists.compare_exchange_strong(expected, true)) {
    logger.Warning(
        "Profiling is already enabled for another SDK instance in this process: the "
        "profiler is shared by the whole process, so only one instance can use it"
    );
    // Another backend holds the slot; only one may drive dd-win-prof at a time.
    return nullptr;
  }
  return std::make_unique<DdWinProfBackend>(logger, api);
}

std::unique_ptr<IProfilerBackend> CreateDdWinProfBackendForTest(
    const DiagnosticLogger& logger, DdWinProfApi api
) {
  return create_backend(logger, api);
}

std::unique_ptr<IProfilerBackend> CreateProfilerBackend(
    const DiagnosticLogger& logger
) {
  return create_backend(
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
