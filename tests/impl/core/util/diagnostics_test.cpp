// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/util/diagnostics.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "datadog/core.h"
#include "datadog/core.hpp"

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

using CopiedMessage = std::pair<DiagnosticLevel, std::string>;

/**
 * Handles a C-API diagnostic message callback by converting the message to the
 * equivalent C++ type, reinterpreting userdata as a pointer to a vector of messages,
 * and pushing the message onto it.
 */
static void my_c_handler(const dd_diagnostic_message_t* message, void* userdata) {
  REQUIRE(userdata);
  auto* messages_ptr = reinterpret_cast<std::vector<CopiedMessage>*>(userdata);
  std::vector<CopiedMessage>& messages = *messages_ptr;
  DiagnosticMessage cpp_message = DiagnosticMessage_FromC(*message);
  messages.emplace_back(cpp_message.level, cpp_message.text);
}

TEST_CASE("DiagnosticLogger", "[unit][diagnostics]") {
  // Given a vector that will accumulate messages received via the handler callback that
  // we provide to a DiagnosticLogger instance
  std::vector<CopiedMessage> messages;

  // And a message-handler callback that buffers all values received into that vector
  auto cpp_handler = [&messages](const DiagnosticMessage& message) {
    messages.emplace_back(message.level, message.text);
  };

  SECTION("M do nothing W default-initalized") {
    // When we produce a message from a default-initialized logger
    DiagnosticLogger{}.Error("oh no");

    // Then nothing crashes, and we don't buffer any messages since the logger isn't
    // even aware of our messages vector
    REQUIRE(messages.size() == 0);
  }

  SECTION("M do nothing W initalized with null callback") {
    // When we explicitly initialize a logger with a null handler callback and log a
    // message that exceeds our configured threshold
    DiagnosticLogger{nullptr, DiagnosticLevel::Warning}.Error("oh no");

    // Then nothing happens, just as above
    REQUIRE(messages.size() == 0);
  }

  SECTION("M invoke handler callback W message emitted") {
    // When we properly initialize a logger and log a message that exceeds our
    // configured threshold
    DiagnosticLogger{cpp_handler, DiagnosticLevel::Warning}.Error("oh no");

    // Then our handler function is invoked for that message
    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0].first == DiagnosticLevel::Error);
    REQUIRE(messages[0].second == "oh no");
  }

  SECTION("M invoke handler callback W message emitted {FromC}") {
    // When we initialize an identical logger in the C API layer and log the same
    // message
    DiagnosticLogger::FromC(my_c_handler, &messages, DD_DIAGNOSTIC_LEVEL_WARNING)
        .Error("oh no");

    // Then our handler function is invoked for that message, just the same
    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0].first == DiagnosticLevel::Error);
    REQUIRE(messages[0].second == "oh no");
  }

  SECTION("M format message with attribute values W log call includes attribute list") {
    // When we properly initialize a logger and log a message with a list of custom
    // attribute values
    DiagnosticLogger{cpp_handler, DiagnosticLevel::Debug}.Status(
        "this is a message", {{"foo", 42}, {"bar", "hello world"}, {"baz", true}}
    );

    // Then our handler function is invoked for that message
    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0].first == DiagnosticLevel::Status);
    REQUIRE(
        messages[0].second ==
        R"(this is a message {"foo":42,"bar":"hello world","baz":true})"
    );
  }

  SECTION("M only emit messages W level meets or exceeds threshold") {
    // Given a function that will initialize two loggers at the given threshold and emit
    // a single message at each level from both loggers
    auto emit_all = [&](DiagnosticLevel cpp_level, dd_diagnostic_level_t c_level) {
      DiagnosticLogger cpp_logger{cpp_handler, cpp_level};
      cpp_logger.Debug("debug");
      cpp_logger.Status("status");
      cpp_logger.Warning("warning");
      cpp_logger.Error("error");

      auto c_logger = DiagnosticLogger::FromC(my_c_handler, &messages, c_level);
      c_logger.Debug("debug");
      c_logger.Status("status");
      c_logger.Warning("warning");
      c_logger.Error("error");
    };

    // And a set of functions that we can use to validate the set of messages emitted
    auto require_debug = [&](size_t i) {
      REQUIRE(messages[i].first == DiagnosticLevel::Debug);
      REQUIRE(messages[i].second == "debug");
    };
    auto require_status = [&](size_t i) {
      REQUIRE(messages[i].first == DiagnosticLevel::Status);
      REQUIRE(messages[i].second == "status");
    };
    auto require_warning = [&](size_t i) {
      REQUIRE(messages[i].first == DiagnosticLevel::Warning);
      REQUIRE(messages[i].second == "warning");
    };
    auto require_error = [&](size_t i) {
      REQUIRE(messages[i].first == DiagnosticLevel::Error);
      REQUIRE(messages[i].second == "error");
    };

    // When we make all 8 log calls at each configured treshold, we get the expected set
    // of messages, with log output become less verbose as the threshold increases
    SECTION("{Debug}") {
      emit_all(DiagnosticLevel::Debug, DD_DIAGNOSTIC_LEVEL_DEBUG);
      REQUIRE(messages.size() == 8);
      require_debug(0);
      require_status(1);
      require_warning(2);
      require_error(3);
      require_debug(4);
      require_status(5);
      require_warning(6);
      require_error(7);
    }

    SECTION("{Status}") {
      emit_all(DiagnosticLevel::Status, DD_DIAGNOSTIC_LEVEL_STATUS);
      REQUIRE(messages.size() == 6);
      require_status(0);
      require_warning(1);
      require_error(2);
      require_status(3);
      require_warning(4);
      require_error(5);
    }

    SECTION("{Warning}") {
      emit_all(DiagnosticLevel::Warning, DD_DIAGNOSTIC_LEVEL_WARNING);
      REQUIRE(messages.size() == 4);
      require_warning(0);
      require_error(1);
      require_warning(2);
      require_error(3);
    }

    SECTION("{Error}") {
      emit_all(DiagnosticLevel::Error, DD_DIAGNOSTIC_LEVEL_ERROR);
      REQUIRE(messages.size() == 2);
      require_error(0);
      require_error(1);
    }
  }
}
