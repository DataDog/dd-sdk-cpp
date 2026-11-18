// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/dd_win_prof_profiler_backend.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
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

bool DD_WIN_PROF_CALL
FakeSetupCallback(ProfilingDataCallback new_callback, void* user_data) {
  if (new_callback == nullptr) {
    calls.push_back("unregister");
    if (!unregister_result) {
      return false;
    }
  } else {
    calls.push_back("register");
    if (!register_result) {
      return false;
    }
  }
  callback = new_callback;
  callback_context = user_data;
  return true;
}

bool FakeSetup(const ProfilerConfig*) {
  calls.push_back("setup");
  return setup_result;
}

bool FakeStart() {
  calls.push_back("start");
  return start_result;
}

void FakeStop() { calls.push_back("stop"); }

const char* FakeErrorMessage(uint32_t) noexcept { return error_message.c_str(); }

DdWinProfApi FakeApi() {
  return {FakeSetupCallback, FakeSetup, FakeStart, FakeStop, FakeErrorMessage};
}

std::unique_ptr<IProfilerBackend> MakeBackend(
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
  return CreateDdWinProfBackendForTest(logger, FakeApi());
}

ProfilingData MakeData() {
  ProfilingData data{};
  data.struct_size = sizeof(data);
  data.start_unix_time_ms = 1700000000123;
  data.end_unix_time_ms = 1700000060456;
  data.is_last = 1;
  return data;
}

}  // namespace

TEST_CASE("DdWinProfBackend copies borrowed callback data", "[unit][profiling]") {
  auto backend = MakeBackend();
  std::optional<ProfileData> received;
  REQUIRE(backend->SetProfileCallback([&](ProfileData profile) {
    received = std::move(profile);
  }));

  SECTION("profile bytes, timestamps, and final flag") {
    std::vector<uint8_t> borrowed{1, 2, 3, 4};
    ProfilingData data = MakeData();
    data.pprof_bytes = borrowed.data();
    data.pprof_size = borrowed.size();

    callback(&data, callback_context);
    borrowed.assign(borrowed.size(), 0);

    REQUIRE(received.has_value());
    REQUIRE(received->pprof == std::vector<uint8_t>{1, 2, 3, 4});
    REQUIRE(received->error.empty());
    REQUIRE(
        received->start.time_since_epoch() == std::chrono::milliseconds(1700000000123)
    );
    REQUIRE(
        received->end.time_since_epoch() == std::chrono::milliseconds(1700000060456)
    );
    REQUIRE(received->is_last);
  }

  SECTION("error text") {
    ProfilingData data = MakeData();
    // Consumers may only distinguish success from failure and ask the profiler to
    // explain an opaque nonzero result.
    data.result = 42;

    callback(&data, callback_context);
    error_message.assign("overwritten");

    REQUIRE(received.has_value());
    REQUIRE(received->pprof.empty());
    REQUIRE(received->error == "profile failed");
    REQUIRE(received->is_last);
  }
}

TEST_CASE("DdWinProfBackend contains callback exceptions", "[unit][profiling]") {
  DiagnosticMessageBuffer diagnostics;
  auto backend = MakeBackend(diagnostics.CreateTestLogger());
  ProfilingData data = MakeData();

  SECTION("standard exception") {
    REQUIRE(backend->SetProfileCallback([](ProfileData) {
      throw std::runtime_error("consumer failed");
    }));
    REQUIRE_NOTHROW(callback(&data, callback_context));
    REQUIRE(diagnostics.warning.back() == "Profiling data callback threw an exception");
  }

  SECTION("allocation failure") {
    REQUIRE(backend->SetProfileCallback([](ProfileData) { throw std::bad_alloc(); }));
    REQUIRE_NOTHROW(callback(&data, callback_context));
    REQUIRE(
        diagnostics.warning.back() ==
        "Could not allocate memory for a profiler callback"
    );
  }
}

TEST_CASE("DdWinProfBackend validates borrowed views", "[unit][profiling]") {
  DiagnosticMessageBuffer diagnostics;
  auto backend = MakeBackend(diagnostics.CreateTestLogger());
  int deliveries = 0;
  REQUIRE(backend->SetProfileCallback([&](ProfileData) { deliveries++; }));

  SECTION("pprof pointer") {
    ProfilingData data = MakeData();
    data.pprof_size = 1;
    callback(&data, callback_context);

    REQUIRE(deliveries == 0);
    REQUIRE(
        diagnostics.warning.back() == "Profiler delivered an invalid pprof byte view"
    );
  }

  SECTION("smaller structure") {
    ProfilingData data = MakeData();
    data.struct_size = sizeof(data) - 1;
    callback(&data, callback_context);

    REQUIRE(deliveries == 0);
    REQUIRE(
        diagnostics.warning.back() ==
        "Profiler delivered an invalid profiling data view"
    );
  }

  SECTION("larger append-only structure") {
    ProfilingData data = MakeData();
    data.struct_size = sizeof(data) + 1;
    callback(&data, callback_context);

    REQUIRE(deliveries == 1);
    REQUIRE(diagnostics.warning.empty());
  }
}

TEST_CASE("DdWinProfBackend ignores delivery without a callback", "[unit][profiling]") {
  DiagnosticMessageBuffer diagnostics;
  auto backend = MakeBackend(diagnostics.CreateTestLogger());
  int deliveries = 0;
  REQUIRE(backend->SetProfileCallback([&](ProfileData) { deliveries++; }));
  const ProfilingDataCallback delivery = callback;
  void* const context = callback_context;
  REQUIRE(backend->SetProfileCallback({}));

  delivery(nullptr, context);

  REQUIRE(deliveries == 0);
  REQUIRE(diagnostics.TotalSize() == 0);
}

TEST_CASE("DdWinProfBackend stops without a callback", "[unit][profiling]") {
  auto backend = MakeBackend();

  backend->Stop();

  REQUIRE(calls == std::vector<std::string>{"stop"});
  backend.reset();
  REQUIRE(calls == std::vector<std::string>{"stop", "stop"});
}

TEST_CASE(
    "DdWinProfBackend unregisters before releasing callback", "[unit][profiling]"
) {
  auto backend = MakeBackend();
  auto lifetime = std::make_shared<int>(0);
  std::weak_ptr<int> weak_lifetime = lifetime;
  REQUIRE(backend->SetProfileCallback([lifetime](ProfileData) {}));
  lifetime.reset();

  backend->Stop();

  REQUIRE(calls == std::vector<std::string>{"register", "stop", "unregister"});
  REQUIRE(weak_lifetime.expired());
  backend.reset();
  REQUIRE(calls == std::vector<std::string>{"register", "stop", "unregister", "stop"});
}

TEST_CASE("DdWinProfBackend unregisters failure paths", "[unit][profiling]") {
  SECTION("registration failure") {
    auto backend = MakeBackend();
    register_result = false;

    REQUIRE_FALSE(backend->SetProfileCallback([](ProfileData) {}));
    REQUIRE(calls == std::vector<std::string>{"register", "unregister"});
  }

  SECTION("setup failure") {
    auto backend = MakeBackend();
    REQUIRE(backend->SetProfileCallback([](ProfileData) {}));
    setup_result = false;

    REQUIRE_FALSE(backend->Init({}));
    REQUIRE(calls == std::vector<std::string>{"register", "setup", "unregister"});
  }

  SECTION("partial start failure") {
    auto backend = MakeBackend();
    REQUIRE(backend->SetProfileCallback([](ProfileData) {}));
    REQUIRE(backend->Init({}));
    start_result = false;

    REQUIRE_FALSE(backend->Start());
    REQUIRE(
        calls ==
        std::vector<std::string>{"register", "setup", "start", "stop", "unregister"}
    );
  }
}
