// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <initializer_list>
#include <mutex>
#include <string_view>
#include <vector>

#include "datadog/core.h"
#include "datadog/core.hpp"

#include "datadog/impl/types/diagnostics.hpp"

#include "support/catch.hpp"

using namespace datadog;

/**
 * Helper struct for use in API-layer tests: buffers all diagnostic messages emitted by
 * a fully-functional SDK instance.
 */
struct DiagnosticMessageBuffer {
  mutable std::mutex mutex;
  std::vector<std::string> debug;
  std::vector<std::string> status;
  std::vector<std::string> warning;
  std::vector<std::string> error;

  /**
   * Returns the total number of diagnostic messages contained in the buffer.
   */
  size_t TotalSize() const {
    std::lock_guard lock(mutex);
    return debug.size() + status.size() + warning.size() + error.size();
  }

  /**
   * Configures an SDK instance initialized via the C API to route all diagnostic
   * messages to this struct.
   */
  void ConfigureC(dd_core_config_t* config) {
    dd_core_config_set_diagnostic_handler_userdata(config, this);
    dd_core_config_set_diagnostic_handler(
        config, [](const dd_diagnostic_message_t* message, void* userdata) {
          auto buf_ptr = reinterpret_cast<DiagnosticMessageBuffer*>(userdata);
          std::lock_guard lock(buf_ptr->mutex);
          switch (message->level) {
            case DD_DIAGNOSTIC_LEVEL_DEBUG:
              buf_ptr->debug.emplace_back(message->text);
              break;
            case DD_DIAGNOSTIC_LEVEL_STATUS:
              buf_ptr->status.emplace_back(message->text);
              break;
            case DD_DIAGNOSTIC_LEVEL_WARNING:
              buf_ptr->warning.emplace_back(message->text);
              break;
            case DD_DIAGNOSTIC_LEVEL_ERROR:
              buf_ptr->error.emplace_back(message->text);
              break;
          }
        }
    );
  }

  /**
   * Creates a DiagnosticHandler function that will copy any messages received into the
   * vectors held by this DiagnosticMessageBuffer. Uses the types defined in the C++
   * API (i.e. datadog::DiagnosticHandler), which are also used internally within the
   * implementation layer.
   */
  DiagnosticHandler CreateHandler() {
    return [this](const DiagnosticMessage& message) {
      std::lock_guard lock(mutex);
      switch (message.level) {
        case DiagnosticLevel::Debug:
          debug.emplace_back(message.text);
          break;
        case DiagnosticLevel::Status:
          status.emplace_back(message.text);
          break;
        case DiagnosticLevel::Warning:
          warning.emplace_back(message.text);
          break;
        case DiagnosticLevel::Error:
          error.emplace_back(message.text);
          break;
      }
    };
  }

  /**
   * Configures an SDK instance initialized via the C++ API to route all diagnostic
   * messages to this struct.
   */
  void ConfigureCpp(CoreConfig& config) {
    config.SetDiagnosticHandler(CreateHandler());
  }

  /**
   * Creates a DiagnosticLogger for use in unit tests: any and all messages emitted via
   * that DiagnosticLogger will be copied into this DiagnosticMessageBuffer.
   */
  impl::DiagnosticLogger CreateTestLogger() {
    return impl::DiagnosticLogger(CreateHandler(), DiagnosticLevel::Debug);
  }
};
