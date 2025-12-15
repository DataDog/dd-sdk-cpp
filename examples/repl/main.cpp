// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "repl/args.hpp"
#include "repl/buffer.hpp"
#include "repl/commands.hpp"
#include "repl/state.hpp"

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)

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

  return CommandResult::Error("Unrecognized command.");
}

void EchoInput(const State& state, std::string_view line) {
  std::string_view sanitized_line = line;
  if (line.find("set-config client-token ") != std::string_view::npos) {
    sanitized_line = "set-config client-token ***********************************";
  }
  for (size_t i = 0; i < state.num_files; i++) {
    std::cout << ">";
  }
  std::cout << "> " << sanitized_line << "\n" << std::flush;
}

void PrintResult(const CommandResult& result) {
  if (result.ok) {
    std::cout << "\033[32m< " << result.message << "\033[0m\n";
  } else {
    std::cout << "\033[35mERROR: " << result.message << "\033[0m\n";
  }
}

bool Run(State& state, std::string_view line) {
  EchoInput(state, line);
  CommandResult result = Handle(state, CommandInput::Parse(line));
  PrintResult(result);
  return result.ok;
}

bool SetConfig(State& state, std::string_view name, std::string_view value) {
  Buffer buffer;
  return Run(
      state,
      buffer.Writef(  // NOLINT(cppcoreguidelines-pro-type-vararg)
          "set-config %.*s %.*s", name.size(), name.data(), value.size(), value.data()
      )
  );
}

bool RunFile(State& state, std::string_view name) {
  // Run 'source <name>' to open the given file
  Buffer buffer;
  const size_t i = state.num_files;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
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
      PrintResult(CommandResult::OK("std::ifstream::close()"));
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
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
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
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
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
    if (!Run(state, "set-config tracking-consent granted")) {
      return 1;
    }
    if (!Run(state, "create-core")) {
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
        PrintResult(CommandResult::OK("std::ifstream::close()"));
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
    CommandResult result = Handle(state, input);
    PrintResult(result);
    if (!result.ok && args.abort_on_error) {
      return 1;
    }
  }
  return 0;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
