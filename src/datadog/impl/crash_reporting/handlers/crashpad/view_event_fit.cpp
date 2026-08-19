// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/handlers/crashpad/view_event_fit.hpp"

#include <cstring>
#include <optional>

#include "datadog/impl/core/json/json_scanner.hpp"

namespace datadog::impl {

namespace {
constexpr std::string_view kEmptyJson = "{}";
}

FitViewEventResult FitViewEventToBufferImpl(
    std::string_view view_event_json, char* buf, size_t buf_size
) {
  // The fast path (event fits as-is) is handled by the template wrapper in the header;
  // this function is only called when truncation is needed.
  const size_t max_bytes = buf_size;
  using Status = FitViewEventResult::Status;

  // Pass 1: find the top-level `context` property value in the JSON object.
  size_t context_value_start = 0;
  size_t context_value_end = 0;
  bool context_found = false;

  {
    JsonScanner scanner{view_event_json};
    if (!scanner.EnterObject()) {
      return {kEmptyJson, Status::Dropped};
    }

    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("context")) {
        context_value_start = scanner.pos;
        scanner.SkipValue();
        if (!scanner.OK()) {
          return {kEmptyJson, Status::Dropped};
        }
        context_value_end = scanner.pos;
        context_found = true;
        break;
      }
      scanner.SkipObjectProperty();
      scanner.SkipObjectPropertySeparator();
    }
  }

  // If `context` was not found, or its value is not a JSON object, fall through to
  // the fallback path
  if (!context_found || view_event_json[context_value_start] != '{') {
    return {kEmptyJson, Status::Dropped};
  }

  // Pass 2: prefix-trim the context object to fit within the available space.
  const std::string_view prefix = view_event_json.substr(0, context_value_start);
  const std::string_view suffix = view_event_json.substr(context_value_end);

  // The minimum space required for the surrounding structure (prefix + "{}" + suffix)
  // before we can even attempt trimming
  if (prefix.size() + suffix.size() > max_bytes - 2) {
    // Even an empty context object won't fit; fall through to fallback
    return {kEmptyJson, Status::Dropped};
  }

  const size_t target_context_size = max_bytes - prefix.size() - suffix.size();

  if (target_context_size < 2) {
    // No room even for `{}`
    return {kEmptyJson, Status::Dropped};
  }

  const std::string_view context_value = view_event_json.substr(
      context_value_start, context_value_end - context_value_start
  );

  // Walk the context entries and find the largest prefix that fits.
  // Reconstructed context is `{` + context_value[1..last_valid_cut) + `}`,
  // which is last_valid_cut + 1 bytes (the opening `{` at index 0, plus one new `}`).
  std::optional<size_t> last_valid_cut;

  {
    JsonScanner scanner{context_value};
    if (scanner.EnterObject()) {
      while (scanner.OK() && scanner.Peek() != '}') {
        if (!scanner.SkipObjectProperty()) {
          break;
        }
        const size_t entry_end = scanner.pos;
        // entry_end + 1: the opening `{` at byte 0, plus bytes [1..entry_end), plus a
        // new closing `}`
        if (entry_end + 1 <= target_context_size) {
          last_valid_cut = entry_end;
        }
        scanner.SkipObjectPropertySeparator();
      }
    }
  }

  // Helper: append bytes into buf, advancing len.
  size_t len = 0;
  const auto append = [&](std::string_view s) {
    std::memcpy(buf + len, s.data(), s.size());
    len += s.size();
  };

  if (last_valid_cut.has_value()) {
    // Build the trimmed payload: prefix + `{` + context entries up to cut + `}` +
    // suffix
    append(prefix);
    append(context_value.substr(0, *last_valid_cut));
    append("}");
    append(suffix);
    return {std::string_view{buf, len}, Status::Truncated};
  }

  // No individual entry fit; try replacing context with `{}`
  const size_t empty_context_size = prefix.size() + 2 + suffix.size();
  if (empty_context_size <= max_bytes) {
    append(prefix);
    append("{}");
    append(suffix);
    return {std::string_view{buf, len}, Status::Truncated};
  }

  // Fallback: entire event dropped
  return {kEmptyJson, Status::Dropped};
}

}  // namespace datadog::impl
