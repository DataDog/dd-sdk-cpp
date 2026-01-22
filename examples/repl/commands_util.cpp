// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <charconv>
#include <chrono>
#include <iostream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#endif

#include "repl/buffer.hpp"
#include "repl/commands.hpp"
#include "repl/state.hpp"

CommandResult HandleSource(State& state, const CommandInput& args) {
  auto pos = args.Positional();
  auto filename = pos[0];
  if (filename.empty()) {
    return CommandResult::Error("First argument to 'source' must be a filename!");
  }
  if (state.num_files >= state.files.max_size()) {
    return CommandResult::Error("Exceeded stack size limit for sourced files!");
  }

  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)

  // While libc++ allows passing std::string_view to std::ifstream::open as a C++17
  // extension, libstdc++ does not: so we need to copy the input filename into a buffer
  // to ensure it's null-terminated
  char filename_buf[260];
  if (filename.size() >= std::size(filename_buf)) {
    return CommandResult::Error("Exceeded character limit for filename!");
  }
  filename.copy(static_cast<char*>(filename_buf), filename.size());
  filename_buf[filename.size()] = '\0';

  const size_t i = state.num_files;
  state.files[i].open(static_cast<const char*>(filename_buf));
  if (!state.files[i].is_open()) {
    return CommandResult::Error("Failed to open sourced file!");
  }
  state.num_files++;

  // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

  return CommandResult::OK("std::ifstream::open()");
}

CommandResult HandleSleep(State&, const CommandInput& args) {
  auto pos = args.Positional();
  int interval_ms = 0;
  if (!pos[0].empty()) {
    std::string_view s = pos[0];
    auto res = std::from_chars(s.data(), s.data() + s.size(), interval_ms);
    if (res.ec != std::errc{} || interval_ms < 0) {
      return CommandResult::Error(
          "First argument to 'sleep' must be a non-negative integer"
      );
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  return CommandResult::OK("std::this_thread::sleep_for()");
}

CommandResult HandleUrl(State& state, const CommandInput& args) {
  if (!state.has_rum_application_id || state.application_id == datadog::UUID::Zero) {
    return CommandResult::Error("RUM Application ID has not been configured!");
  }

  std::string_view subdomain = "app";
  if (!args.Peek().empty()) {
    subdomain = args.Peek();
  }

  char encoded_uuid_bytes[36];
  state.application_id.ToBytes(static_cast<char*>(encoded_uuid_bytes), 36);

  Buffer buffer;
  std::string_view url = buffer.Writef(  // NOLINT(cppcoreguidelines-pro-type-vararg)
      "https://%.*s.datadoghq.com/rum/"
      "sessions?query=%%40type%%3Asession%%20%%40application.id%%3A%.*s",
      subdomain.size(),
      subdomain.data(),
      36,
      static_cast<const char*>(encoded_uuid_bytes)
  );
  std::cout << url << "\n";
  return CommandResult::OK("URL printed.");
}

CommandResult HandleNop(State&, const CommandInput& args) {
  auto message = Unquote(args[0]);
  return CommandResult::OK(message.empty() ? "nop" : message);
}

CommandResult HandleCrash(State&, const CommandInput&) {
#ifdef _WIN32
  // Record an access violation: if the SDK has installed a vectored exception handler,
  // the system's SEH handler will call it, allowing the crash to be handled before the
  // process terminates
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#else
  // Deliver SIGSEGV to this process: if the SDK has registered a signal handler, the
  // kernel will call it, allowing the crash to be handled before the process terminates
  raise(SIGSEGV);
#endif

  // This code is unreachable if we crash as intended; there can be no successful result
  return CommandResult::Error("HandleCrash did not crash!");
}
