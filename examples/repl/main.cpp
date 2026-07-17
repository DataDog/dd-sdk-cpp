// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "repl/alloc.hpp"
#include "repl/args.hpp"
#include "repl/buffer.hpp"
#include "repl/commands.hpp"
#include "repl/state.hpp"

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
CommandResult Handle(State& state, const CommandInput& input) {
  // Utility
  if (input.Peek() == "source") {
    return HandleSource(state, input.Shift());
  }
  if (input.Peek() == "sleep") {
    return HandleSleep(state, input.Shift());
  }
  if (input.Peek() == "url") {
    return HandleUrl(state, input.Shift());
  }
  if (input.Peek() == "nop") {
    return HandleNop(state, input.Shift());
  }

  // Profile
  if (input.Peek() == "start-profile") {
    return HandleStartProfile(state, input.Shift());
  }
  if (input.Peek() == "stop-profile") {
    return HandleStopProfile(state, input.Shift());
  }

  // datadog::CoreConfig, datadog::RumConfig
  if (input.Peek() == "set-config") {
    return HandleSetConfig(state, input.Shift());
  }

  // datadog::Core
  if (input.Peek() == "create-core") {
    return HandleCreateCore(state, input.Shift());
  }
  if (input.Peek() == "reset-core") {
    return HandleResetCore(state, input.Shift());
  }
  if (input.Peek() == "set-tracking-consent") {
    return HandleSetTrackingConsent(state, input.Shift());
  }
  if (input.Peek() == "set-user-info") {
    return HandleSetUserInfo(state, input.Shift());
  }
  if (input.Peek() == "add-user-extra-info") {
    return HandleAddUserExtraInfo(state, input.Shift());
  }
  if (input.Peek() == "clear-user-info") {
    return HandleClearUserInfo(state, input.Shift());
  }
  if (input.Peek() == "set-account-info") {
    return HandleSetAccountInfo(state, input.Shift());
  }
  if (input.Peek() == "add-account-extra-info") {
    return HandleAddAccountExtraInfo(state, input.Shift());
  }
  if (input.Peek() == "clear-account-info") {
    return HandleClearAccountInfo(state, input.Shift());
  }
  if (input.Peek() == "start-core") {
    return HandleStartCore(state, input.Shift());
  }
  if (input.Peek() == "stop-core") {
    return HandleStopCore(state, input.Shift());
  }

  // datadog::Logging
  if (input.Peek() == "register-logging") {
    return HandleRegisterLogging(state, input.Shift());
  }
  if (input.Peek() == "create-logger") {
    return HandleCreateLogger(state, input.Shift());
  }
  if (input.Peek() == "log") {
    return HandleLog(state, input.Shift());
  }

  // datadog::Rum
  if (input.Peek() == "register-rum") {
    return HandleRegisterRum(state, input.Shift());
  }
  if (input.Peek() == "stop-session") {
    return HandleStopSession(state, input.Shift());
  }
  if (input.Peek() == "start-view") {
    return HandleStartView(state, input.Shift());
  }
  if (input.Peek() == "stop-view") {
    return HandleStopView(state, input.Shift());
  }
  if (input.Peek() == "add-action") {
    return HandleAddAction(state, input.Shift());
  }
  if (input.Peek() == "start-action") {
    return HandleStartAction(state, input.Shift());
  }
  if (input.Peek() == "stop-action") {
    return HandleStopAction(state, input.Shift());
  }
  if (input.Peek() == "start-resource") {
    return HandleStartResource(state, input.Shift());
  }
  if (input.Peek() == "stop-resource") {
    return HandleStopResource(state, input.Shift());
  }
  if (input.Peek() == "stop-resource-with-error") {
    return HandleStopResourceWithError(state, input.Shift());
  }
  if (input.Peek() == "add-error") {
    return HandleAddError(state, input.Shift());
  }
  if (input.Peek() == "start-operation") {
    return HandleStartOperation(state, input.Shift());
  }
  if (input.Peek() == "succeed-operation") {
    return HandleSucceedOperation(state, input.Shift());
  }
  if (input.Peek() == "fail-operation") {
    return HandleFailOperation(state, input.Shift());
  }
  if (input.Peek() == "report-app-display-initialized") {
    return HandleReportAppDisplayInitialized(state, input.Shift());
  }
  if (input.Peek() == "report-app-fully-displayed") {
    return HandleReportAppFullyDisplayed(state, input.Shift());
  }

  // datadog::CrashReporting
  if (input.Peek() == "register-crash-reporting") {
    return HandleRegisterCrashReporting(state, input.Shift());
  }
  if (input.Peek() == "crash") {
    return HandleCrash(state, input.Shift());
  }

  return CommandResult::Error("Unrecognized command.");
}

void EchoInput(const State& state, std::string_view line) {
  std::string_view sanitized_line = line;
  if (line.find("set-config client-token ") != std::string_view::npos) {
    sanitized_line = "set-config client-token ***********************************";
  }
  // Print '>' by default, '>>' if executing a sourced file, etc.; adding another
  // character for each level of nesting
  char prompt_buf[MAX_SOURCED_FILES + 2];
  const size_t num_prompt_chars = state.num_files + 1;
  for (size_t i = 0; i < num_prompt_chars; i++) {
    prompt_buf[i] = '>';
  }
  prompt_buf[num_prompt_chars] = '\0';
  std::cout << state.output_buffer.Writef(
      "%s %.*s\n",
      static_cast<const char*>(prompt_buf),
      sanitized_line.size(),
      sanitized_line.data()
  );
}

void PrintResult(Buffer& output_buffer, const CommandResult& result) {
  if (result.ok) {
    std::cout << output_buffer.Writef(
        "\033[32m< %.*s\033[0m\n", result.message.size(), result.message.data()
    );
  } else {
    std::cout << output_buffer.Writef(
        "\033[35mERROR: %.*s\033[0m\n", result.message.size(), result.message.data()
    );
  }
}

bool Run(State& state, std::string_view line) {
  EchoInput(state, line);
  CommandResult result = Handle(state, CommandInput::Parse(line));
  PrintResult(state.output_buffer, result);
  return result.ok;
}

bool SetConfig(State& state, std::string_view name, std::string_view value) {
  Buffer buffer;
  return Run(
      state,
      buffer.Writef(
          "set-config %.*s %.*s", name.size(), name.data(), value.size(), value.data()
      )
  );
}

bool RunFile(State& state, std::string_view name) {
  // Run 'source <name>' to open the given file
  Buffer buffer;
  const size_t i = state.num_files;
  if (!Run(state, buffer.Writef("source %.*s", name.size(), name.data()))) {
    return false;
  }
  if (state.num_files != i + 1) {
    return false;
  }

  // Process the commands in that file until we hit EOF, then close it
  while (true) {
    auto [input_line, eof] = buffer.GetLine(state.files[i]);
    if (eof) {
      state.files[--state.num_files].close();
      PrintResult(state.output_buffer, CommandResult::OK("std::ifstream::close()"));
      break;
    }
    if (input_line.empty()) {
      continue;
    }
    if (!Run(state, input_line)) {
      return false;
    }
  }
  return true;
}

int main(int argc, char* argv[]) {  // NOLINT
#ifdef _WIN32
  bool interactive = true;
#else
  bool interactive = isatty(STDIN_FILENO);
#endif

  // Parse command-line args
  Args args = Args::Parse(argc, argv);

  // If using --autostart, validate config up-front to ensure that misconfiguration
  // errors are clearly distinguished from SDK errors
  if (args.autostart) {
    // If we have --client-token and --rum-application-id args, we're good
    if (args.client_token.empty() || args.rum_application_id.empty()) {
      // Otherwise, our autostart routine will attempt to load those values by running
      // 'source .repl-env', so preemptively verify that the target file exists and
      // contains the required credentials
      bool repl_env_has_client_token = false;
      bool repl_env_has_rum_application_id = false;
      std::ifstream repl_env(".repl-env");
      if (repl_env.is_open()) {
        Buffer buffer;
        while (true) {
          auto [line, eof] = buffer.GetLine(repl_env);
          if (eof) {
            break;
          }
          if (line.find("set-config client-token ") != std::string_view::npos) {
            repl_env_has_client_token = true;
          }
          if (line.find("set-config rum-application-id ") != std::string_view::npos) {
            repl_env_has_rum_application_id = true;
          }
        }
        repl_env.close();
      }

      // If it doesn't, fail with a descriptive error message rather than allowing
      // initialization to proceed
      if (!repl_env_has_client_token || !repl_env_has_rum_application_id) {
        std::cerr << "ERROR: If using --autostart, you must provide required SDK "
                     "config values.\n";
        std::cerr << "Either supply --client-token <value> and --rum-application-id "
                     "<value>,\n";
        std::cerr << "or create a file called .repl-env that includes:\n";
        std::cerr << "  set-config client-token <value>\n";
        std::cerr << "  set-config rum-application-id <value>\n";
        return 1;
      }
    }
  }

  // Establish all the Datadog SDK state that the REPL can work with, along with REPL
  // execution state that commands may access or modify
  State state;

  // Print diagnostic messages as REPL output, color-coded
  state.config.SetDiagnosticThreshold(datadog::DiagnosticLevel::Debug);
  state.config.SetDiagnosticHandler([&](const datadog::DiagnosticMessage& message) {
    Buffer buffer;
    static const char* level_names[] = {"DEBUG", "STATUS", "WARNING", "ERROR"};
    const size_t i = static_cast<size_t>(message.level);
    const char* level_name = i < std::size(level_names) ? level_names[i] : "";

    static const char* ansi_starts[] = {"\033[36m", "\033[97m", "\033[33m", "\033[31m"};
    static const char* ansi_dark = "\033[90m";
    static const char* ansi_reset = "\033[0m";
    const char* ansi_start = i < std::size(ansi_starts) ? ansi_starts[i] : "";

    const char* json_start = std::strchr(message.text, '{');
    if (json_start) {
      const size_t json_start_pos = json_start - message.text;
      const size_t message_text_len = json_start_pos;
      std::cout << buffer.Writef(
          "\r%s[DATADOG %s] %.*s%s%s%s\n",
          ansi_start,
          level_name,
          message_text_len,
          message.text,
          ansi_dark,
          json_start,
          ansi_reset
      );
    } else {
      std::cout << buffer.Writef(
          "\r%s[DATADOG %s] %s%s\n", ansi_start, level_name, message.text, ansi_reset
      );
    }

    if (message.level == datadog::DiagnosticLevel::Error && args.abort_on_error) {
      std::abort();
    }
    if (message.level == datadog::DiagnosticLevel::Warning && args.abort_on_warning) {
      std::abort();
    }
  });

  // Run commands to apply any SDK config values supplied via command-line args
  if (!args.client_token.empty()) {
    if (!SetConfig(state, "client-token", args.client_token)) {
      return 1;
    }
  }
  if (!args.service.empty()) {
    if (!SetConfig(state, "service", args.service)) {
      return 1;
    }
  }
  if (!args.env.empty()) {
    if (!SetConfig(state, "env", args.env)) {
      return 1;
    }
  }
  if (!args.custom_endpoint_url.empty()) {
    if (!SetConfig(state, "custom-endpoint-url", args.custom_endpoint_url)) {
      return 1;
    }
  }
  if (!args.rum_application_id.empty()) {
    if (!SetConfig(state, "rum-application-id", args.rum_application_id)) {
      return 1;
    }
  }
  if (!args.application_storage_path.empty()) {
    if (!SetConfig(state, "application-storage-path", args.application_storage_path)) {
      return 1;
    }
  }

  // Support --autostart for interactive use: run a standard initialization routine that
  // gives us all features and a running SDK ready to receive API calls that generate
  // events
  if (args.autostart) {
    // If we weren't given both --client-token and --rum-application-id args, source a
    // file called .repl-env
    if (args.client_token.empty() || args.rum_application_id.empty()) {
      if (!RunFile(state, ".repl-env")) {
        return 1;
      }
    }

    // Prepare an SDK instance with all features and start it
    if (!Run(state, "create-core tracking-consent:granted")) {
      return 1;
    }
    if (!Run(state, "register-logging")) {
      return 1;
    }
    if (!Run(state, "register-rum")) {
      return 1;
    }
    if (!Run(state, "create-logger")) {
      return 1;
    }
    if (!Run(state, "start-core")) {
      return 1;
    }
  }

  // Process commands line-by-line until we get 'exit' or the input stream is closed
  Buffer input_buffer;
  while (true) {
    // If we expect direct user input on stdin, print a prompt
    if (interactive && state.num_files == 0) {
      std::cout << "> " << std::flush;
    }

    // Read the next line of input from the input stream
    auto [input_line, eof] = input_buffer.GetLine(
        state.num_files > 0 ? state.files[state.num_files - 1] : std::cin
    );

    // If we've reached the end of the input stream, handle it
    if (eof) {
      // If we've hit EOF on a sourced file, close it and pop it off the stack
      if (state.num_files > 0) {
        state.files[--state.num_files].close();  // NOLINT
        PrintResult(state.output_buffer, CommandResult::OK("std::ifstream::close()"));
        continue;
      }

      // If we've hit EOF from stdin, exit the program
      break;
    }

    if (input_line.empty()) {
      continue;
    }
    if (!interactive || state.num_files > 0) {
      EchoInput(state, input_line);
    }
    if (input_line == "exit") {
      break;
    }
    CommandInput input = CommandInput::Parse(input_line);
    auto start_time = std::chrono::steady_clock::now();
    CommandResult result = Handle(state, input);
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    PrintResult(state.output_buffer, result);
    if (!result.ok && args.abort_on_error) {
      return 1;
    }

    if (result.ok && state.is_profiling) {
      std::cout << state.output_buffer.Writef(
          "<<(duration): %" PRId64 "\n",
          std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
      );

#if WITH_DATADOG_ALLOCATION_TRACKING
      if (state.is_profiling_allocations) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        auto events = ReadAvailableAllocationEvents();
        std::cout << state.output_buffer.Writef(
            "<<(alloc-events): %zu\n", events.second
        );
        if (events.second > 0) {
          for (size_t i = 0; i < events.second; i++) {
            AllocEvent* ev = events.first + i;
            const char* op = ev->op == AllocOp::Alloc ? "alloc" : "free";
            std::cout << state.output_buffer.Writef(
                "<<<(%zu): op:%s size:%zu tid:%zu\n", i, op, ev->size, ev->tid
            );
          }
        }
      }
#endif
    }
  }
  return 0;
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
