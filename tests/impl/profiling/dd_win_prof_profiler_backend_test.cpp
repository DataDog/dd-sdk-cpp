// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/dd_win_prof_profiler_backend.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/catch.hpp"
#include "support/diagnostics.hpp"

using namespace datadog::impl;

namespace {

ProfilingDataCallback callback;
void* callback_context;
bool register_result;
bool unregister_result;
bool setup_result;
bool start_result;
std::string error_message;
std::vector<std::string> calls;

static bool DD_WIN_PROF_CALL
fake_setup_callback(ProfilingDataCallback new_callback, void* user_data) {
  if (new_callback == nullptr) {
    calls.push_back("unregister");
    if (!unregister_result) {
      // Simulated unregistration failed; preserve the previous callback registration.
      return false;
    }
  } else {
    calls.push_back("register");
    if (!register_result) {
      // Simulated registration failed; preserve the previous callback registration.
      return false;
    }
  }
  callback = new_callback;
  callback_context = user_data;
  return true;
}

static bool fake_setup(const ProfilerConfig*) {
  calls.push_back("setup");
  return setup_result;
}

static bool fake_start() {
  calls.push_back("start");
  return start_result;
}

static void fake_stop() { calls.push_back("stop"); }

static const char* fake_error_message(uint32_t) noexcept {
  return error_message.c_str();
}

static DdWinProfApi fake_api() {
  return {fake_setup_callback, fake_setup, fake_start, fake_stop, fake_error_message};
}

static std::unique_ptr<IProfilerBackend> make_backend(
    DiagnosticLogger logger = DiagnosticLogger{}
) {
  callback = nullptr;
  callback_context = nullptr;
  register_result = true;
  unregister_result = true;
  setup_result = true;
  start_result = true;
  error_message = "profile failed";
  calls.clear();
  auto backend = CreateDdWinProfBackendForTest(logger, fake_api());
  // Fails if an earlier test leaked a backend, which would still hold the slot
  REQUIRE(backend != nullptr);
  return backend;
}

static ProfilingData make_data() {
  ProfilingData data{};
  data.struct_size = sizeof(data);
  data.start_unix_time_ms = 1700000000123;
  data.end_unix_time_ms = 1700000060456;
  data.is_last = 1;
  return data;
}

}  // namespace

TEST_CASE("DdWinProfBackend delivers borrowed callback data", "[unit][profiling]") {
  DiagnosticMessageBuffer diagnostics;
  auto backend = make_backend(diagnostics.CreateTestLogger());
  int deliveries = 0;
  std::vector<uint8_t> received_pprof;
  std::string received_error;
  Timestamp received_start;
  Timestamp received_end;
  bool received_is_last = false;
  REQUIRE(backend->SetProfileCallback([&](const ProfileView& profile) {
    deliveries++;
    // The view is only valid during the callback, so copy what the test checks
    received_pprof.assign(profile.pprof.begin(), profile.pprof.end());
    received_error = profile.error;
    received_start = profile.start;
    received_end = profile.end;
    received_is_last = profile.is_last;
  }));

  SECTION("profile bytes, timestamps, and final flag") {
    const std::vector<uint8_t> borrowed{1, 2, 3, 4};
    ProfilingData data = make_data();
    data.pprof_bytes = borrowed.data();
    data.pprof_size = borrowed.size();

    callback(&data, callback_context);

    REQUIRE(deliveries == 1);
    REQUIRE(received_pprof == borrowed);
    REQUIRE(received_error.empty());
    REQUIRE(
        received_start.time_since_epoch() == std::chrono::milliseconds(1700000000123)
    );
    REQUIRE(
        received_end.time_since_epoch() == std::chrono::milliseconds(1700000060456)
    );
    REQUIRE(received_is_last);
  }

  SECTION("empty profile") {
    ProfilingData data = make_data();

    callback(&data, callback_context);

    REQUIRE(deliveries == 1);
    REQUIRE(received_pprof.empty());
    REQUIRE(diagnostics.warning.empty());
  }

  SECTION("export error") {
    ProfilingData data = make_data();
    // Consumers may only distinguish success from failure and ask the profiler to
    // explain an opaque nonzero result.
    data.result = 42;

    callback(&data, callback_context);

    // The failed period is still delivered, so the consumer can discard its data
    REQUIRE(deliveries == 1);
    REQUIRE(received_error == "profile failed");
    REQUIRE(received_pprof.empty());
    REQUIRE(
        received_start.time_since_epoch() == std::chrono::milliseconds(1700000000123)
    );
    REQUIRE(
        received_end.time_since_epoch() == std::chrono::milliseconds(1700000060456)
    );
    REQUIRE(received_is_last);
    REQUIRE(
        diagnostics.warning.back() ==
        R"(Profiler failed to export a profile {"result":42,"error":"profile failed"})"
    );
  }

  SECTION("export error without a description") {
    ProfilingData data = make_data();
    data.result = 42;
    error_message.clear();

    callback(&data, callback_context);

    REQUIRE(deliveries == 1);
    REQUIRE(received_error == "Unknown profiler error");
  }
}

TEST_CASE("DdWinProfBackend contains callback exceptions", "[unit][profiling]") {
  DiagnosticMessageBuffer diagnostics;
  auto backend = make_backend(diagnostics.CreateTestLogger());
  ProfilingData data = make_data();

  SECTION("standard exception") {
    REQUIRE(backend->SetProfileCallback([](const ProfileView&) {
      throw std::runtime_error("consumer failed");
    }));
    REQUIRE_NOTHROW(callback(&data, callback_context));
    REQUIRE(diagnostics.warning.back() == "Profiling data callback threw an exception");
  }
}

TEST_CASE("DdWinProfBackend validates borrowed views", "[unit][profiling]") {
  DiagnosticMessageBuffer diagnostics;
  auto backend = make_backend(diagnostics.CreateTestLogger());
  int deliveries = 0;
  std::string received_error;
  REQUIRE(backend->SetProfileCallback([&](const ProfileView& profile) {
    deliveries++;
    received_error = profile.error;
  }));

  SECTION("pprof pointer") {
    ProfilingData data = make_data();
    data.pprof_size = 1;
    callback(&data, callback_context);

    // The period is reported as failed rather than dropped
    REQUIRE(deliveries == 1);
    REQUIRE(received_error == "Profiler delivered an invalid pprof byte view");
    REQUIRE(
        diagnostics.warning.back() == "Profiler delivered an invalid pprof byte view"
    );
  }

  SECTION("smaller structure") {
    ProfilingData data = make_data();
    data.struct_size = sizeof(data) - 1;
    callback(&data, callback_context);

    REQUIRE(deliveries == 0);
    REQUIRE(
        diagnostics.warning.back() ==
        "Profiler delivered an invalid profiling data view"
    );
  }

  SECTION("larger append-only structure") {
    ProfilingData data = make_data();
    data.struct_size = sizeof(data) + 1;
    callback(&data, callback_context);

    REQUIRE(deliveries == 1);
    REQUIRE(diagnostics.warning.empty());
  }
}

TEST_CASE("DdWinProfBackend ignores delivery without a callback", "[unit][profiling]") {
  DiagnosticMessageBuffer diagnostics;
  auto backend = make_backend(diagnostics.CreateTestLogger());
  int deliveries = 0;
  REQUIRE(backend->SetProfileCallback([&](const ProfileView&) { deliveries++; }));
  const ProfilingDataCallback delivery = callback;
  void* const context = callback_context;
  REQUIRE(backend->SetProfileCallback({}));

  delivery(nullptr, context);

  REQUIRE(deliveries == 0);
  REQUIRE(diagnostics.TotalSize() == 0);
}

TEST_CASE("DdWinProfBackend stops without a callback", "[unit][profiling]") {
  auto backend = make_backend();

  backend->Stop();

  REQUIRE(calls == std::vector<std::string>{"stop"});
  backend.reset();
  REQUIRE(calls == std::vector<std::string>{"stop", "stop"});
}

TEST_CASE(
    "DdWinProfBackend unregisters before releasing callback", "[unit][profiling]"
) {
  auto backend = make_backend();
  auto lifetime = std::make_shared<int>(0);
  std::weak_ptr<int> weak_lifetime = lifetime;
  REQUIRE(backend->SetProfileCallback([lifetime](const ProfileView&) {}));
  lifetime.reset();

  backend->Stop();

  REQUIRE(calls == std::vector<std::string>{"register", "stop", "unregister"});
  REQUIRE(weak_lifetime.expired());
  backend.reset();
  REQUIRE(calls == std::vector<std::string>{"register", "stop", "unregister", "stop"});
}

TEST_CASE("DdWinProfBackend unregisters failure paths", "[unit][profiling]") {
  SECTION("registration failure") {
    auto backend = make_backend();
    register_result = false;

    REQUIRE_FALSE(backend->SetProfileCallback([](const ProfileView&) {}));
    REQUIRE(calls == std::vector<std::string>{"register"});

    // A callback that was never registered isn't unregistered at teardown
    backend.reset();
    REQUIRE(calls == std::vector<std::string>{"register", "stop"});
  }

  SECTION("setup failure") {
    auto backend = make_backend();
    REQUIRE(backend->SetProfileCallback([](const ProfileView&) {}));
    setup_result = false;

    REQUIRE_FALSE(backend->Init({}));
    REQUIRE(calls == std::vector<std::string>{"register", "setup", "unregister"});
  }

  SECTION("partial start failure") {
    auto backend = make_backend();
    REQUIRE(backend->SetProfileCallback([](const ProfileView&) {}));
    REQUIRE(backend->Init({}));
    start_result = false;

    REQUIRE_FALSE(backend->Start());
    REQUIRE(
        calls ==
        std::vector<std::string>{"register", "setup", "start", "stop", "unregister"}
    );
  }
}

TEST_CASE(
    "DdWinProfBackend test backends claim the process-wide slot", "[unit][profiling]"
) {
  DiagnosticMessageBuffer diagnostics;

  // When a test backend exists
  auto backend = make_backend();

  // Then no other backend can be created until it's destroyed
  REQUIRE(CreateProfilerBackend(diagnostics.CreateTestLogger()) == nullptr);
  REQUIRE(CreateDdWinProfBackendForTest(DiagnosticLogger{}, fake_api()) == nullptr);
  backend.reset();
  REQUIRE(CreateDdWinProfBackendForTest(DiagnosticLogger{}, fake_api()) != nullptr);
}
