// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/handlers/crashpad/view_event_fit.hpp"

#include <cstring>
#include <optional>

#include "datadog/impl/types/assert.hpp"
#include "datadog/impl/types/json/json_scanner.hpp"

namespace datadog::impl {

FitViewEventResult FitViewEventToBufferImpl(
    std::string_view view_event_json, char* buf, size_t buf_size
) {
  // The fast path (event fits as-is) is handled by the template wrapper in the header;
  // this function is only called when truncation is needed.

  // Run an initial JsonScanner pass to locate the span within the JSON payload
  // representing the value of the "context" property
  size_t context_value_start = 0;
  size_t context_value_end = 0;
  bool context_found = false;
  {
    JsonScanner scanner{view_event_json};
    if (!scanner.EnterObject()) {
      return {"{}", FitViewEventResult::Status::Dropped};
    }
    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("context")) {
        context_value_start = scanner.pos;
        scanner.SkipValue();
        if (!scanner.OK()) {
          return {"{}", FitViewEventResult::Status::Dropped};
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
    return {"{}", FitViewEventResult::Status::Dropped};
  }

  // Now that we've located the `context` value, split on that span to get everything
  // preceding it (including `,"context":`) and everything following it
  const std::string_view prefix = view_event_json.substr(0, context_value_start);
  const std::string_view suffix = view_event_json.substr(context_value_end);

  // Compute the size required for (prefix + "{}" + suffix) - this is the smallest
  // possible value we could produce if we truncated down to `"context":{}`, so if this
  // still won't fit, we can't fit the value at all
  const size_t min_size_with_no_context_properties = prefix.size() + 2 + suffix.size();
  if (min_size_with_no_context_properties > buf_size) {
    return {"{}", FitViewEventResult::Status::Dropped};
  }

  // Compute how much space we have available for the context properties that _will_ fit
  const size_t available_context_size = buf_size - prefix.size() - suffix.size();
  DATADOG_ASSERT(available_context_size >= 2, "unable to fit {} when trimming context");

  // Walk the context entries and find the largest prefix that fits the available size.
  // Reconstructed context is `{` + context_value[1..last_valid_cut) + `}`, which is
  // `last_valid_cut + 1` bytes
  const std::string_view context_value = view_event_json.substr(
      context_value_start, context_value_end - context_value_start
  );
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
        if (entry_end + 1 <= available_context_size) {
          last_valid_cut = entry_end;
        }
        scanner.SkipObjectPropertySeparator();
      }
    }
  }

  // Use a helper to append bytes into buf, advancing len
  size_t len = 0;
  const auto append = [&](std::string_view s) {
    std::memcpy(buf + len, s.data(), s.size());
    len += s.size();
  };

  // If we identified a cutoff point, accept the prefix up to the start of the context
  // value, then append the substring from the context value that fits, cap it with '}',
  // and append any remaining suffix from the top-level payload
  if (last_valid_cut.has_value()) {
    append(prefix);
    append(context_value.substr(0, *last_valid_cut));
    append("}");
    append(suffix);
    return {std::string_view{buf, len}, FitViewEventResult::Status::Truncated};
  }

  // If no context values at all will fit, but we still have enough space for the rest
  // of the event, just write the original event with '{}' in place of the context value
  // (we've previously validated that buf_size >= min_size_with_no_context_properties,
  // so we're guaranteed to have space here)
  append(prefix);
  append("{}");
  append(suffix);
  return {std::string_view{buf, len}, FitViewEventResult::Status::Truncated};
}

}  // namespace datadog::impl
