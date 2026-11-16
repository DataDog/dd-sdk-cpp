// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace datadog::impl {

/**
 * Result of FitViewEventToBuffer. `value` is always a valid JSON value that fits
 * within the annotation buffer; `status` indicates what happened:
 *
 * - Ok:        event fit as-is; `out_buf` was not touched.
 * - Truncated: context entries were dropped to make it fit; caller should log a
 *              warning.
 * - Dropped:   even an empty context didn't fit (or context was absent/non-object);
 *              `value` is "{}"; caller should log an error.
 */
struct FitViewEventResult {
  enum class Status { Ok, Truncated, Dropped };

  std::string_view value;
  Status status;
};

/**
 * Non-template implementation core: assembles a truncated view event into the raw
 * buffer at `buf` (capacity `buf_size` bytes), returning a FitViewEventResult.
 * Called only by FitViewEventToBuffer below; not intended for direct use.
 */
FitViewEventResult FitViewEventToBufferImpl(
    std::string_view view_event_json, char* buf, size_t buf_size
);

/**
 * Given a pre-encoded RUM View event JSON string, returns a FitViewEventResult whose
 * `value` fits within N bytes, suitable for use as a Crashpad string annotation value.
 *
 * If `view_event_json` fits as-is (size <= N), returns it with status Ok and does not
 * touch `out_buf`.
 *
 * If it doesn't fit, uses JsonScanner to locate the top-level `context` property
 * value. If found and the value is a JSON object (starts with `{`), applies
 * prefix-trimming to the context entries until the whole payload fits, assembles the
 * result in `out_buf`, and returns it with status Truncated.
 *
 * If the payload still doesn't fit even with `context` replaced by `{}` (or `context`
 * was absent, or its value was not a JSON object), returns "{}" with status Dropped
 * without writing to `out_buf`.
 *
 * N must be >= 2.
 */
template <size_t N>
FitViewEventResult FitViewEventToBuffer(
    std::string_view view_event_json, std::array<char, N>& out_buf
) {
  static_assert(N >= 2);
  if (view_event_json.size() <= N) {
    return {view_event_json, FitViewEventResult::Status::Ok};
  }
  return FitViewEventToBufferImpl(view_event_json, out_buf.data(), N);
}

}  // namespace datadog::impl
