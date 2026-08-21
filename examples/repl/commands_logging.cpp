// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <charconv>
#include <optional>

#include "datadog.hpp"

#include "repl/commands.hpp"
#include "repl/state.hpp"

CommandResult HandleRegisterLogging(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  if (state.logging) {
    return CommandResult::Error("Logging already registered!");
  }
  state.logging = datadog::Logging::Register(state.core);
  if (!state.logging) {
    return CommandResult::Error("Logging::Register() failed!");
  }
  return CommandResult::OK("Logging::Register()");
}

CommandResult HandleCreateLogger(State& state, const CommandInput&) {
  if (!state.logging) {
    return CommandResult::Error("Logging is not registered!");
  }
  if (state.num_loggers >= state.loggers.max_size()) {
    return CommandResult::Error("Logger limit reached!");
  }
  auto logger = state.logging->CreateLogger();
  if (!logger) {
    return CommandResult::Error("Logging::CreateLogger() failed!");
  }
  state.loggers[state.num_loggers++] = std::move(logger);  // NOLINT
  return CommandResult::OK("Logging::CreateLogger()");
}

CommandResult HandleAddLoggingAttribute(State& state, const CommandInput& args) {
  if (!state.logging) {
    return CommandResult::Error("Logging is not registered!");
  }
  auto named = args.Named();
  auto attrs = CollectAttributes(named);
  if (attrs) {
    for (size_t i = 0; i < attrs->GetObjectPropertyCount(); i++) {
      state.logging->AddAttribute(
          attrs->GetObjectPropertyNameAt(static_cast<int>(i)),
          attrs->GetObjectPropertyValueAt(static_cast<int>(i))
      );
    }
  }
  return CommandResult::OK("Logging::AddAttribute()");
}

CommandResult HandleAddLoggerAttribute(State& state, const CommandInput& args) {
  if (!state.logging) {
    return CommandResult::Error("Logging is not registered!");
  }
  auto named = args.Named();
  size_t logger_index = 0;
  std::string_view logger_str = named.Get("logger");
  if (!logger_str.empty()) {
    auto res = std::from_chars(
        logger_str.data(), logger_str.data() + logger_str.size(), logger_index
    );
    if (res.ec != std::errc{}) {
      return CommandResult::Error("Value for 'logger' must specify logger index!");
    }
  }
  if (logger_index >= state.num_loggers) {
    return CommandResult::Error("No logger at specified index!");
  }
  auto attrs = CollectAttributes(named);
  if (attrs) {
    for (size_t i = 0; i < attrs->GetObjectPropertyCount(); i++) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
      state.loggers[logger_index]->AddAttribute(
          attrs->GetObjectPropertyNameAt(static_cast<int>(i)),
          attrs->GetObjectPropertyValueAt(static_cast<int>(i))
      );
    }
  }
  return CommandResult::OK("Logger::AddAttribute()");
}

CommandResult HandleAddLoggerTag(State& state, const CommandInput& args) {
  if (!state.logging) {
    return CommandResult::Error("Logging is not registered!");
  }
  auto named = args.Named();
  size_t logger_index = 0;
  std::string_view logger_str = named.Get("logger");
  if (!logger_str.empty()) {
    auto res = std::from_chars(
        logger_str.data(), logger_str.data() + logger_str.size(), logger_index
    );
    if (res.ec != std::errc{}) {
      return CommandResult::Error("Value for 'logger' must specify logger index!");
    }
  }
  if (logger_index >= state.num_loggers) {
    return CommandResult::Error("No logger at specified index!");
  }
  for (size_t i = 0; i < named.n; i++) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
    std::string_view name = named.values[i].name;
    std::string_view value = named.values[i].value;
    if (name == "logger") {
      continue;
    }
    state.loggers[logger_index]->AddTag(name, Unquote(value));
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
  }
  // Also handle positional args with no ":" as valueless tags
  auto pos = args.Positional();
  for (size_t i = 0; i < pos.n; i++) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    state.loggers[logger_index]->AddTag(Unquote(pos[i]));
  }
  return CommandResult::OK("Logger::AddTag()");
}

CommandResult HandleLog(State& state, const CommandInput& args) {
  if (!state.logging) {
    return CommandResult::Error("Logging is not registered!");
  }

  // Positional args
  auto pos = args.Positional();
  std::string_view message = Unquote(pos[0]);
  if (message.empty()) {
    message = "This is a log message.";
  }

  // Named args
  auto named = args.Named();

  size_t logger_index = 0;
  std::string_view logger_str = named.Get("logger");
  if (!logger_str.empty()) {
    std::string_view s = logger_str;
    auto res = std::from_chars(s.data(), s.data() + s.size(), logger_index);
    if (res.ec != std::errc{}) {
      return CommandResult::Error("Value for 'logger' must specify logger index!");
    }
  }
  if (logger_index >= state.num_loggers) {
    return CommandResult::Error("No logger at specified index!");
  }

  datadog::LogLevel level = datadog::LogLevel::Info;
  std::string_view level_str = named.Get("level");
  if (!level_str.empty()) {
    if (level_str == "debug") {
      level = datadog::LogLevel::Debug;
    } else if (level_str == "info") {
      level = datadog::LogLevel::Info;
    } else if (level_str == "notice") {
      level = datadog::LogLevel::Notice;
    } else if (level_str == "warn" || level_str == "warning") {
      level = datadog::LogLevel::Warn;
    } else if (level_str == "error") {
      level = datadog::LogLevel::Error;
    } else if (level_str == "critical") {
      level = datadog::LogLevel::Critical;
    } else {
      return CommandResult::Error("Invalid log level!");
    }
  }

  auto attrs = CollectAttributes(named);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  state.loggers[logger_index]->Log(
      level, message, {}, attrs.value_or(datadog::Attribute{})
  );
  return CommandResult::OK("Logger::Log()");
}
