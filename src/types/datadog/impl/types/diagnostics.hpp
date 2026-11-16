// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <climits>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <variant>

#include "datadog/attribute.hpp"
#include "datadog/core.h"
#include "datadog/core.hpp"
#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Union of the possible primitive value types that can be included in diagnostic log
 * messages as named attributes.
 */
using DiagnosticAttributeValue =
    std::variant<bool, int64_t, uint64_t, double, Timestamp, UUID, std::string_view>;

namespace detail {

/**
 * Constructs a DiagnosticAttributeValue from an arbitrary value. The integer overloads
 * exist because, on 32-bit targets (e.g. armv7), `size_t` is `unsigned int`, which
 * matches neither `int64_t` nor `uint64_t` in the variant; routing `int`/`unsigned int`
 * through `int64_t` resolves the otherwise-ambiguous variant construction.
 */
template <typename T>
DiagnosticAttributeValue ToDiagnosticValue(T&& value) {
  return DiagnosticAttributeValue(std::forward<T>(value));
}
#if UINT_MAX != UINT64_MAX
inline DiagnosticAttributeValue ToDiagnosticValue(int value) {
  return DiagnosticAttributeValue(static_cast<int64_t>(value));
}
inline DiagnosticAttributeValue ToDiagnosticValue(unsigned int value) {
  return DiagnosticAttributeValue(static_cast<int64_t>(value));
}
#endif

}  // namespace detail

/**
 * A single named attribute in a diagnostic log message.
 *
 * Call sites construct these via brace-initialization, e.g. `{{"key", value}, ...}`. We
 * use a dedicated type rather than std::pair because brace-initializing
 * `std::pair<std::string_view, DiagnosticAttributeValue>` fails to compile with Clang
 * 15 on 32-bit targets (e.g. armv7); constructing the variant explicitly in the
 * constructor avoids that.
 */
struct DiagnosticAttribute {
  std::string_view key;
  DiagnosticAttributeValue value;

  template <typename T>
  DiagnosticAttribute(std::string_view in_key, T&& in_value)
      : key(in_key), value(detail::ToDiagnosticValue(std::forward<T>(in_value))) {}
};

/**
 * A static list of attribute values to include in a specific log message. Serialized as
 * a JSON object; see `json/diagnostic_attribute.hpp`.
 */
using DiagnosticAttributeList = std::initializer_list<DiagnosticAttribute>;

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
