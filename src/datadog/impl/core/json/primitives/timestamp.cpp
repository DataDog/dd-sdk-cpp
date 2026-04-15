// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/json/primitives/timestamp.hpp"

#include <charconv>
#include <chrono>

#include "date/date.h"

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

// "YYYY-MM-DDTHH:MM:SS.sssZ": 24 chars + 2 for quotes
static const size_t QUOTED_ISO8601_LEN = 26;

static void _write_timestamp_4d(char*& ptr, size_t n, uint64_t value) {
  auto res = std::to_chars(ptr, ptr + n, value);
  DATADOG_ASSERT(res.ec == std::errc{}, "insufficient buffer space on timestamp write");
  DATADOG_ASSERT(res.ptr == ptr + 4, "unexpected write size on timestamp write");
  ptr += 4;
}

static void _write_timestamp_02d(char*& ptr, size_t n, uint64_t value) {
  DATADOG_ASSERT(value <= 99, "value out of range for 02d");
  DATADOG_ASSERT(n >= 2, "insufficient buffer space for 02d");

  static const char digits[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
  if (value < 10) {
    *ptr++ = '0';
    *ptr++ = digits[value];  // NOLINT
  } else {
    auto res = std::to_chars(ptr, ptr + n, value);
    DATADOG_ASSERT(
        res.ec == std::errc{}, "insufficient buffer space on timestamp write"
    );
    DATADOG_ASSERT(res.ptr == ptr + 2, "unexpected write size on timestamp write");
    ptr += 2;
  }
}

static void _write_timestamp_03d(char*& ptr, size_t n, uint64_t value) {
  DATADOG_ASSERT(value <= 999, "value out of range for 03d");
  DATADOG_ASSERT(n >= 3, "insufficient buffer space for 03d");

  if (value < 100) {
    *ptr++ = '0';
    n--;
  }

  if (value < 10) {
    *ptr++ = '0';
    n--;
  }

  auto res = std::to_chars(ptr, ptr + n, value);
  DATADOG_ASSERT(res.ec == std::errc{}, "insufficient buffer space on timestamp write");

  const size_t num_written = res.ptr - ptr;
  DATADOG_ASSERT(
      num_written == (value < 10 ? 1 : (value < 100 ? 2 : 3)),
      "unexpected write size on timestamp write"
  );
  ptr += num_written;
}

size_t GetJsonSize(const Timestamp& value) {
  (void)value;
  return QUOTED_ISO8601_LEN;
}

size_t WriteJson(char* dst, size_t n, const Timestamp& value) {
  DATADOG_ASSERT(n >= QUOTED_ISO8601_LEN, "insufficient buffer size for timestamp");

  // Use HowardHinnant/date to compute an accurate calendar date (YYYY-MM-DD) from our
  // std::chrono::time_point, reducing precision to seconds first so that we don't cause
  // signed integer overflow when `date::floor` performs conversions on values near
  // INT64_MIN
  auto value_seconds = std::chrono::time_point_cast<std::chrono::seconds>(value);
  auto day_point = date::floor<date::days>(value_seconds);
  date::year_month_day ymd = date::year_month_day{day_point};

  // Also using date, compute a time of day (HH:MM:SS) from our original time_point: we
  // also need to do our arithmetic with reduced precision here to avoid overflow (at
  // least on libc++, which uses int rather than long long for std::chrono::days), so we
  // need to compute the subsecond remainder and tack it on after conversion
  auto sec_of_day = value_seconds - day_point;
  auto subseconds = value - value_seconds;
  date::hh_mm_ss time = date::make_time(sec_of_day + subseconds);

  // Sanity check: an int64 Unix timestamp in nanoseconds can only represent years
  // within this range
  const int year_value = static_cast<int>(ymd.year());
  DATADOG_ASSERT(year_value >= 1677, "date::floor yielded year before 1677");
  DATADOG_ASSERT(year_value <= 2262, "date::floor yielded year after 2262");

  // Get unsigned values for our calendar date: [1677..2262], [1..12], [1..31]
  const uint64_t year = year_value;
  const uint64_t month = static_cast<unsigned>(ymd.month());
  const uint64_t day = static_cast<unsigned>(ymd.day());

  // Get unsigned values for our wall-clock time: [0..23], [0..59], [0..59]
  const uint64_t hours = time.hours().count();
  const uint64_t minutes = time.minutes().count();
  const uint64_t seconds = time.seconds().count();

  // Get subsecond milliseconds [0..999]
  std::chrono::milliseconds subsec =
      std::chrono::duration_cast<std::chrono::milliseconds>(time.subseconds());
  const uint64_t millis = subsec.count();

  // date::format() returns a std::string - to avoid the overhead of allocating every
  // time we write a timestamp, we can instead handle the formatting ourselves with
  // std::to_chars
  char* ptr = dst;
  char* const dst_end = dst + n;

  // Write an opening quote to begin our JSON string literal
  *ptr++ = '"';

  // Write YYYY-MM-DD
  _write_timestamp_4d(ptr, dst_end - ptr, year);
  *ptr++ = '-';
  _write_timestamp_02d(ptr, dst_end - ptr, month);
  *ptr++ = '-';
  _write_timestamp_02d(ptr, dst_end - ptr, day);

  // Write T to delimit date from time
  *ptr++ = 'T';

  // Write HH:MM:SS.sss
  _write_timestamp_02d(ptr, dst_end - ptr, hours);
  *ptr++ = ':';
  _write_timestamp_02d(ptr, dst_end - ptr, minutes);
  *ptr++ = ':';
  _write_timestamp_02d(ptr, dst_end - ptr, seconds);
  *ptr++ = '.';
  _write_timestamp_03d(ptr, dst_end - ptr, millis);

  // Write trailing Z
  *ptr++ = 'Z';

  // Write closing quote
  *ptr++ = '"';

  // We should have ended up with a value that's exactly 26 bytes, representing a quoted
  // JSON literal string in ISO-8601 format, with millisecond precision
  const size_t num_bytes_written = ptr - dst;
  DATADOG_ASSERT(
      num_bytes_written == QUOTED_ISO8601_LEN,
      "unexpected result size for JSON-encoded timestamp"
  );
  return num_bytes_written;
}

}  // namespace datadog::impl
