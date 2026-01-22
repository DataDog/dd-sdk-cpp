// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <variant>

#include "datadog/attribute.hpp"
#include "datadog/core.h"
#include "datadog/core.hpp"
#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/types.hpp"

namespace datadog::impl {

/**
 * Union of the possible primitive value types that can be included in diagnostic log
 * messages as named attributes.
 */
using DiagnosticAttributeValue =
    std::variant<bool, int64_t, uint64_t, double, Timestamp, UUID, std::string_view>;

/**
 * A static list of attribute values to include in a specific log message. Serialized as
 * a JSON object; see `json/diagnostic_attribute.hpp`.
 */
using DiagnosticAttributeList =
    std::initializer_list<std::pair<std::string_view, DiagnosticAttributeValue>>;

/**
 * Wraps a user-provided (or SDK-default) diagnostic message handler callback, providing
 * an interface that lets the SDK signal to the client application when warnings,
 * errors, or status updates occur.
 *
 * DiagnosticLogger encapsulates simple, local-only logging to a user-provided callback;
 * it does not provide more complex telemetry functionality; nor does it overlap with
 * the SDK's Logging feature in any way.
 *
 * We use DiagnosticLogger throughout the API layer, to signal API usage errors
 * irrespective of the internal state of the SDK. DiagnosticLogger is also used within
 * the implementation layer when we need to signal warnings via the same callback
 * mechanism.
 *
 * As DiagnosticLogger simply wraps a `std::function` and an enum value, it is trivially
 * constructible and copyable.
 */
class DiagnosticLogger {
  DiagnosticHandler handler;
  DiagnosticLevel threshold;

 public:
  // A default-initialized DiagnosticLogger handles log calls as no-ops
  DiagnosticLogger() : handler(nullptr), threshold(DiagnosticLevel::Error) {}

  // A DiagnosticLogger is copyable and movable
  DiagnosticLogger(const DiagnosticLogger&) = default;
  DiagnosticLogger& operator=(const DiagnosticLogger&) = default;
  DiagnosticLogger(DiagnosticLogger&&) = default;
  DiagnosticLogger& operator=(DiagnosticLogger&&) = default;

  /**
   * Initializes a new diagnostic-logger interface given a message-handler callback and
   * the threshold at which the callback should be invoked.
   */
  explicit DiagnosticLogger(DiagnosticHandler in_handler, DiagnosticLevel in_threshold)
      : handler(std::move(in_handler)), threshold(in_threshold) {}

  /**
   * Initializes a new diagnostic-logger interface from a C-style callback. The C API
   * accepts a raw function pointer, with an additional `void* userdata` parameter to
   * provide a means of accessing persistent state.
   */
  static DiagnosticLogger FromC(
      dd_diagnostic_handler_t c_handler,
      void* c_handler_userdata,
      dd_diagnostic_level_t c_threshold
  ) {
    // Capture the callback and the userdata value to bind a std::function that will
    // handle messages emitted from the C++ implementation layer, translating them to
    // equivalent C type and invoking the C callback
    DiagnosticHandler cpp_handler = [=](const DiagnosticMessage& cpp_message) {
      if (c_handler) {
        dd_diagnostic_message_t c_message = DiagnosticMessage_ToC(cpp_message);
        c_handler(&c_message, c_handler_userdata);
      }
    };

    // Threshold check occurs _before_ the callback is invoked, in DiagnosticLogger
    DiagnosticLevel cpp_threshold = DiagnosticLevel_FromC(c_threshold);
    return DiagnosticLogger{cpp_handler, cpp_threshold};
  }

  /**
   * Logs a debug message. A debug message provides verbose, low-level details about the
   * state of the SDK.
   */
  void Debug(const char* text, DiagnosticAttributeList attributes = {}) const {
    Emit(DiagnosticLevel::Debug, text, attributes);
  }

  /**
   * Logs a status message. A status message provides relatively infrequent and succinct
   * feedback about the state of the SDK, such as the results of upload attempts.
   */
  void Status(const char* text, DiagnosticAttributeList attributes = {}) const {
    Emit(DiagnosticLevel::Status, text, attributes);
  }

  /**
   * Logs a warning message. A warning usually indicates that the SDK could not fully
   * comply with an API call made from the application (e.g. because the application
   * supplied invalid arguments, the SDK was not in a supported state to handle the
   * operation, etc.) but the SDK continues to operate normally otherwise.
   */
  void Warning(const char* text, DiagnosticAttributeList attributes = {}) const {
    Emit(DiagnosticLevel::Warning, text, attributes);
  }

  /**
   * Logs an error message. An error usually indicates that the SDK could not be
   * initialized or has ceased to function as intended.
   */
  void Error(const char* text, DiagnosticAttributeList attributes = {}) const {
    Emit(DiagnosticLevel::Error, text, attributes);
  }

 private:
  void Emit(
      DiagnosticLevel level, const char* text, DiagnosticAttributeList attributes
  ) const;
};

}  // namespace datadog::impl
