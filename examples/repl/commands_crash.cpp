// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#endif

#include "datadog.hpp"

#include "repl/commands.hpp"
#include "repl/state.hpp"

namespace {

// These routines are intended to provoke intentional crashes by various low-level
// means; they do not need safety checks.
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)

void IntentionalCrash_Raise() {
#ifdef _WIN32
  // Record an access violation: if the SDK has installed an exception handler, the
  // system's SEH handler will call it, allowing the crash to be handled before the
  // process terminates
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#else
  // Deliver SIGSEGV to this process: if the SDK has registered a signal handler, the
  // kernel will call it, allowing the crash to be handled before the process
  // terminates
  raise(SIGSEGV);
#endif
}

void IntentionalCrash_Raise_ThreadMain() {
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  IntentionalCrash_Raise();
}

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4717)  // recursive on all control paths
#endif

void IntentionalCrash_StackOverflow() {
  // Reserve a large buffer to make sure our stack frames are beefy, declaring it
  // volatile and writing to it to ensure that the compiler won't optimize it away
  volatile char buffer[1024];
  buffer[0] = 0;

  // Recurse infinitely
  IntentionalCrash_StackOverflow();
}

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

void IntentionalCrash_StackOverflow_ThreadMain() {
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  IntentionalCrash_StackOverflow();
}

void IntentionalCrash_BadSdkUsage(void* addr) {
  auto* core = reinterpret_cast<datadog::Core*>(addr);
  core->SetTrackingConsent(datadog::TrackingConsent::Pending);
}

void IntentionalCrash_BadSdkUsage_ThreadMain(void* addr) {
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  IntentionalCrash_BadSdkUsage(addr);
}

void IntentionalCrash_BadSdkDtor(State& state) {
  // If we don't already have a datadog::Core, initialize a dummy core so we can use it
  // to provoke our crash
  if (!state.core) {
    state.core = datadog::Core::Create(
        datadog::CoreConfig("fake-client-token", "fake-service", "fake-env")
    );
  }

  // datadog::Core is an API-layer wrapper for a datadog::impl::Core, which is held in
  // the member variable `std::unique_ptr<impl::Core> _impl` at offset 0 (it's the first
  // member)
  datadog::Core* core = state.core.get();
  void** impl_ptr_addr = reinterpret_cast<void**>(core);

  // Corrupt that address so that future calls to Core API functions will crash
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  *impl_ptr_addr = reinterpret_cast<void*>(static_cast<uintptr_t>(0xbeefbe0f));

  // Reset our only reference to the datadog::Core: this will destroy the unique_ptr, in
  // turn invoking impl::Core::~Core() with a corrupt value for the 'this' pointer: this
  // should result in a violation with fault address 0xbeefbe0f, with a callstack that
  // shows several layers of shared_ptr and unique_ptr machinery
  state.core.reset();
}

void IntentionalCrash_BadSdkDtor_ThreadMain(State& state) {
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  IntentionalCrash_BadSdkDtor(state);
}

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

}  // namespace

CommandResult HandleRegisterCrashReporting(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  if (state.crash_reporting) {
    return CommandResult::Error("Crash Reporting already registered!");
  }
  state.crash_reporting =
      datadog::CrashReporting::Register(state.core, state.crash_reporting_config);
  if (!state.crash_reporting) {
    return CommandResult::Error("CrashReporting::Register() failed!");
  }
  return CommandResult::OK("CrashReporting::Register()");
}

CommandResult HandleCrash(State& state, const CommandInput& args) {
  // First positional arg indicates type of crash we want to provoke
  auto pos = args.Positional();
  std::string_view mode = Unquote(pos[0]);
  if (mode.empty()) {
    mode = "raise";
  }

  // If background:true, spin up a background thread to run the faulty code
  auto named = args.Named();
  const bool background = named.GetFlag("background");

  // In the simplest mode, just raise a segfault / access violation directly
  if (mode == "raise") {
    if (background) {
      std::thread t{IntentionalCrash_Raise_ThreadMain};
      t.detach();
      return CommandResult::OK("std::thread::detach()");
    }
    IntentionalCrash_Raise();
    return CommandResult::Error("IntentionalCrash_Raise() did not crash!");
  }

  // If mode is 'bad-sdk-usage', include some SDK calls in the callstack: this will jump
  // to datadog::Core::SetTrackingConsent but with an invalid this pointer derived from
  // our state struct
  if (mode == "bad-sdk-usage") {
    if (background) {
      std::thread t{IntentionalCrash_BadSdkUsage_ThreadMain, &state};
      t.detach();
      return CommandResult::OK("std::thread::detach()");
    }
    IntentionalCrash_BadSdkUsage(&state);
    return CommandResult::Error("IntentionalCrash_BadSdkUsage() did not crash!");
  }

  // If mode is 'bad-sdk-dtor', produce a more elaborate callstack by corrupting the
  // pointer held by our Core, then deleting that Core so that we get a chain of calls
  // leading to a destructor call that ultimately segfaults
  if (mode == "bad-sdk-dtor") {
    if (background) {
      std::thread t{IntentionalCrash_BadSdkDtor_ThreadMain, std::ref(state)};
      t.detach();
      return CommandResult::OK("std::thread::detach()");
    }
    IntentionalCrash_BadSdkDtor(state);
    return CommandResult::Error("IntentionalCrash_BadSdkDtor() did not crash!");
  }

  // If mode is 'stack-overflow', cause a stack overflow via infinite recursion
  if (mode == "stack-overflow") {
    if (background) {
      std::thread t{IntentionalCrash_StackOverflow_ThreadMain};
      t.detach();
      return CommandResult::OK("std::thread::detach()");
    }
    IntentionalCrash_StackOverflow();
    return CommandResult::Error("IntentionalCrash_StackOverflow() did not crash!");
  }

  return CommandResult::Error("Unrecognied mode for crash!");
}
