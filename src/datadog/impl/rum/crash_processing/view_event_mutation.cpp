// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/view_event_mutation.hpp"

#include <array>
#include <charconv>

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

std::string MutateViewEventForCrash(
    std::string_view view_event_json,
    const RumViewEventParser::Spans& spans,
    const RumViewEventParser::Values& values,
    uint64_t crash_timestamp_ms,
    std::string_view context_json
) {
  // These invariants are enforced by `RumViewEventParser::Parse()`, and we should never
  // attempt mutation after a failed parse
  DATADOG_ASSERT(
      spans.HasAllRequiredMatches(),
      "unable to mutate view event with missing property spans"
  );
  DATADOG_ASSERT(
      spans.HasExpectedFieldOrdering(),
      "unable to mutate view event with out-of-order property spans"
  );

  // Sanity-check: the view_event_json value that we pass to this function should be the
  // same value that produced our parse results, so all spans should lie within the
  // range of that input string
  const size_t last_value_end = spans.type.i + spans.type.len;
  DATADOG_ASSERT(
      last_value_end < view_event_json.size(),
      "match for type value is outside the range of view_event_json"
  );

  // The crash timestamp provided is a raw system time reading in milliseconds: the JSON
  // schema uses integer milliseconds for the `date` timestamp, so we can substitute a
  // modified crash timestamp in place of the original `date` value
  static_assert(
      std::is_same_v<decltype(RumViewEvent::date), MilliTimestamp>,
      "type of RumViewEvent::date is not MilliTimestamp"
  );

  // Compute a new timestamp for the end of the view. Note that we don't verify whether
  // this new value is greater than the original `date` value, so it's technically
  // possible for the view's `date` to move backwards as a result of this unconditional
  // change; and we also leave `view.time_spent` unchanged. Both of these behaviors are
  // consistent with the iOS SDK.
  const uint64_t new_date = crash_timestamp_ms - 1;

  // Compute incremented values for view.error.count and _dd.document_version
  const uint64_t new_view_error_count = values.view_error_count + 1;
  const uint64_t new_dd_document_version = values.dd_document_version + 1;

  // Use a helper function to format uint64_t values to decimal strings
  auto format_uint64 = [](uint64_t x, std::array<char, 21>& buf) -> std::string_view {
    auto res = std::to_chars(buf.data(), buf.data() + buf.size(), x);
    DATADOG_ASSERT(res.ec == std::errc{}, "to_chars failed where impossible");
    const size_t len = static_cast<size_t>(res.ptr - buf.data());
    return std::string_view{buf.data(), len};
  };

  // Format the integer values that will be substituted into the result: a uint64_t can
  // not exceed 20 decimal digits
  std::array<char, 21> date_buf{};
  std::array<char, 21> view_error_count_buf{};
  std::array<char, 21> dd_document_version_buf{};
  auto date_str = format_uint64(new_date, date_buf);
  auto view_error_count_str = format_uint64(new_view_error_count, view_error_count_buf);
  auto dd_document_version_str =
      format_uint64(new_dd_document_version, dd_document_version_buf);

  // Establish the raw text of the view.crash object that we'll append after view.error,
  // including a leading comma
  constexpr std::string_view view_crash_str = ",\"crash\":{\"count\":1}";

  // Prefix used when inserting a new context property (no context in original event):
  // the leading comma separates _dd from the new property, and the comma already at
  // dd_end_pos is preserved as the separator before type
  constexpr std::string_view context_key_str = ",\"context\":";

  // Compute the net change in size from inserting or replacing values in our JSON
  // object: substitutions may grow or shrink the string, so we need signed arithmetic
  int64_t size_delta = 0;
  {
    // We're replacing `date` with a different timestamp
    size_delta +=
        static_cast<int64_t>(date_str.size()) - static_cast<int64_t>(spans.date.len);

    // We're replacing `view.is_active` (either true or false) with false
    size_delta += 5 - static_cast<int64_t>(spans.view_is_active.len);

    // We're replacing `view.error.count` with an incremented value
    size_delta += static_cast<int64_t>(view_error_count_str.size()) -
                  static_cast<int64_t>(spans.view_error_count.len);

    // We're inserting an entirely new `view.crash` object
    size_delta += static_cast<int64_t>(view_crash_str.size());

    // We're replacing `_dd.document_version` with an incremented value
    size_delta += static_cast<int64_t>(dd_document_version_str.size()) -
                  static_cast<int64_t>(spans.dd_document_version.len);

    // We're either replacing an existing context value or inserting a new one, but only
    // if `context_json` is provided
    if (!context_json.empty()) {
      if (spans.context.OK()) {
        size_delta += static_cast<int64_t>(context_json.size()) -
                      static_cast<int64_t>(spans.context.len);
      } else {
        size_delta += static_cast<int64_t>(context_key_str.size()) +
                      static_cast<int64_t>(context_json.size());
      }
    }
  }

  // We now know the exact size of our result value: perform a single allocation to
  // reserve exactly the amount of space we need
  const size_t result_size =
      static_cast<size_t>(static_cast<int64_t>(view_event_json.size()) + size_delta);
  std::string result;
  result.reserve(result_size);

  // Use a helper function to append contiguous ranges of the original JSON string into
  // `result`
  auto copy_range = [&view_event_json, &result](size_t from, size_t to) {
    DATADOG_ASSERT(to >= from, "attempted to copy invalid range");
    result.append(view_event_json.data() + from, to - from);
  };

  // Track our read position as we advance through the original string: as we slice the
  // string and concatenate it back together with new values substituted, we rely on the
  // ordering guarantees provided by the parser via HasExpectedFieldOrdering()
  size_t pos = 0;

  // Copy everything up to original date value, then substitute the new date value, then
  // advance our read position past the original date value
  copy_range(pos, spans.date.i);
  result.append(date_str);
  pos = spans.date.i + spans.date.len;

  // Continue copying up to view.is_active, replacing its value with false
  copy_range(pos, spans.view_is_active.i);
  result.append(std::string_view{"false", 5});
  pos = spans.view_is_active.i + spans.view_is_active.len;

  // Copy up to the view.error.count value, replacing its value
  copy_range(pos, spans.view_error_count.i);
  result.append(view_error_count_str);
  pos = spans.view_error_count.i + spans.view_error_count.len;

  // We now need to insert a view.crash object: the parser has validated that the
  // original event did _not_ include view.crash, and it has captured the position of
  // the comma that follows the view.error value. Copy everything up to that point
  // (typically just the closing `}` that follows the view.error.count value), then
  // insert `,"crash":{"count":1}`, then advance to the position of that original
  // trailing comma.
  copy_range(pos, spans.view_error_end_pos);
  result.append(view_crash_str);
  pos = spans.view_error_end_pos;

  // Continue copying up to _dd.document_version, substituting the incremented value
  copy_range(pos, spans.dd_document_version.i);
  result.append(dd_document_version_str);
  pos = spans.dd_document_version.i + spans.dd_document_version.len;

  // If we have a new value for `context`, either replace the existing context value or
  // insert a new one, depending on whether the original value had a top-level context
  // property
  if (!context_json.empty()) {
    if (spans.context.OK()) {
      // 'context' already existed: replace its value entirely and proceed
      copy_range(pos, spans.context.i);
      result.append(context_json);
      pos = spans.context.i + spans.context.len;
    } else {
      // 'context' did not exist: insert a new `,"context":<value>` just before the
      // comma that follows `"_dd":{...}`, resuming the remaining copies from that comma
      copy_range(pos, spans.dd_end_pos);
      result.append(context_key_str);
      result.append(context_json);
      pos = spans.dd_end_pos;
    }
  }

  // Copy the remainder of the original event unchanged
  copy_range(pos, view_event_json.size());

  DATADOG_ASSERT(
      result.size() == result_size, "mutation produced unexpected number of bytes"
  );
  return result;
}

}  // namespace datadog::impl
