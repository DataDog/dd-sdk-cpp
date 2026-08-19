// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/handlers/crashpad/view_event_fit.hpp"

#include <array>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string_view>

#include "support/catch.hpp"

using namespace datadog::impl;
using Status = FitViewEventResult::Status;

namespace {

// Confirm the result is a parseable JSON value
bool IsValidJson(std::string_view s) {
  try {
    static_cast<void>(nlohmann::json::parse(s));
    return true;
  } catch (...) {
    return false;
  }
}

// A minimal well-formed view event with no `context` property (88 bytes)
constexpr std::string_view kSmallEvent =
    R"({"date":1,"_dd":{"format_version":2},"type":"view"})";

// A view event with a non-trivial `context` property
constexpr std::string_view kEventWithContext =
    R"({"date":1,"context":{"a":"1111111111","b":"2222222222","c":"3333333333"},"type":"view"})";

}  // namespace

TEST_CASE("FitViewEventToBuffer", "[unit][crash_reporting]") {
  // Use a large buffer so that no test is constrained by buffer capacity rather than
  // the max_bytes logic under test. The sentinel pattern lets us verify that the fast
  // path doesn't write into the buffer at all.
  std::array<char, 8192> buf{};
  const auto buf_is_untouched = [&] {
    for (const char c : buf) {
      if (c != '\0') {
        return false;
      }
    }
    return true;
  };

  SECTION("M return event unchanged W N exceeds event size") {
    // kSmallEvent is well under 8192 bytes
    const auto [value, status] = FitViewEventToBuffer(kSmallEvent, buf);
    REQUIRE(value == kSmallEvent);
    REQUIRE(status == Status::Ok);
    REQUIRE(buf_is_untouched());
  }

  SECTION("M return event unchanged W N equals event size exactly") {
    // Fast path condition is size() <= N; confirm the boundary is inclusive
    std::array<char, kSmallEvent.size()> exact_buf{};
    const auto [value, status] = FitViewEventToBuffer(kSmallEvent, exact_buf);
    REQUIRE(value == kSmallEvent);
    REQUIRE(status == Status::Ok);
  }

  SECTION("M return {} with Dropped W event has no context property and does not fit") {
    // Use a buffer sized smaller than kSmallEvent to force the truncation path
    std::array<char, 10> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(kSmallEvent, small_buf);
    REQUIRE(value == "{}");
    REQUIRE(status == Status::Dropped);
    REQUIRE(IsValidJson(value));
  }

  SECTION("M return {} with Dropped W context value is not an object") {
    constexpr std::string_view event = R"({"date":1,"context":null,"type":"view"})";
    REQUIRE(event.size() > 10);
    std::array<char, 10> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(event, small_buf);
    REQUIRE(value == "{}");
    REQUIRE(status == Status::Dropped);
    REQUIRE(IsValidJson(value));
  }

  SECTION(
      "M return {} with Dropped W context is empty object and result still too large"
  ) {
    // prefix = {"date":1,"context": (20 bytes), suffix = ,"type":"view"} (15 bytes)
    // total with empty context = 20 + 2 + 15 = 37 bytes; restrict to 36
    constexpr std::string_view event = R"({"date":1,"context":{},"type":"view"})";
    REQUIRE(event.size() == 37);
    std::array<char, 36> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(event, small_buf);
    REQUIRE(value == "{}");
    REQUIRE(status == Status::Dropped);
    REQUIRE(IsValidJson(value));
  }

  SECTION("M return trimmed event with Truncated W context has one droppable entry") {
    // Given a view event with context containing two entries where the second is too
    // large, so only the first entry fits within the budget
    constexpr std::string_view event2 =
        R"({"date":1,"context":{"k":"v","extra":"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"},"type":"view"})";
    // prefix = {"date":1,"context": = 20, suffix = ,"type":"view"} = 15
    // trimmed context = {"k":"v"} = 9 bytes, total = 44
    REQUIRE(event2.size() > 44);
    std::array<char, 44> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(event2, small_buf);
    REQUIRE(status == Status::Truncated);
    REQUIRE(IsValidJson(value));
    const auto parsed = nlohmann::json::parse(value);
    REQUIRE(parsed.contains("context"));
    REQUIRE(parsed["context"].contains("k"));
    REQUIRE(!parsed["context"].contains("extra"));
  }

  SECTION(
      "M retain correct prefix with Truncated W context has multiple entries and last "
      "overflows"
  ) {
    // prefix = {"date":1,"context": (20), suffix = ,"type":"view"} (15)
    // Keep first entry only: {"a":"1111111111"} = 18 bytes; total = 20+18+15 = 53
    REQUIRE(kEventWithContext.size() > 53);
    std::array<char, 53> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(kEventWithContext, small_buf);
    REQUIRE(status == Status::Truncated);
    REQUIRE(value.size() == 53);
    REQUIRE(IsValidJson(value));
    const auto parsed = nlohmann::json::parse(value);
    REQUIRE(parsed.contains("context"));
    REQUIRE(parsed["context"].contains("a"));
    REQUIRE(!parsed["context"].contains("b"));
    REQUIRE(!parsed["context"].contains("c"));
    // Result is properly closed: no trailing comma, valid JSON object
    REQUIRE(value.back() == '}');
  }

  SECTION(
      "M produce context with two entries with Truncated W budget allows two but not "
      "three"
  ) {
    // prefix = {"date":1,"context": (20), suffix = ,"type":"view"} (15)
    // Keep first two entries: {"a":"1111111111","b":"2222222222"} = 35 bytes; total =
    // 20+35+15 = 70
    REQUIRE(kEventWithContext.size() > 70);
    std::array<char, 70> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(kEventWithContext, small_buf);
    REQUIRE(status == Status::Truncated);
    REQUIRE(IsValidJson(value));
    const auto parsed = nlohmann::json::parse(value);
    REQUIRE(parsed["context"].contains("a"));
    REQUIRE(parsed["context"].contains("b"));
    REQUIRE(!parsed["context"].contains("c"));
  }

  SECTION(
      "M return context:{} with Truncated W first context entry alone is too large"
  ) {
    // prefix = {"date":1,"context": (20), suffix = ,"type":"view"} (15)
    // context:{} = 2 bytes; total = 37
    // context with one entry = 18 bytes; total = 53
    // N=40: context:{} fits (37 <= 40) but first entry doesn't (53 > 40)
    REQUIRE(kEventWithContext.size() > 40);
    std::array<char, 40> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(kEventWithContext, small_buf);
    REQUIRE(status == Status::Truncated);
    REQUIRE(IsValidJson(value));
    const auto parsed = nlohmann::json::parse(value);
    REQUIRE(parsed.contains("context"));
    REQUIRE(parsed["context"].empty());
  }

  SECTION("M handle scanner failure mid-context gracefully") {
    // Given a truncated (invalid) context value that causes the scanner to fail
    // partway through; the function should fall back to context:{} or {} rather
    // than crashing or producing invalid JSON
    constexpr std::string_view broken_event = R"({"date":1,"context":{"a":"truncated)";
    std::array<char, 10> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(broken_event, small_buf);
    REQUIRE(IsValidJson(value));
    // Status may be Truncated (context:{} fit) or Dropped depending on sizes; either
    // is acceptable as long as the result is valid JSON
    REQUIRE((status == Status::Truncated || status == Status::Dropped));
  }

  SECTION("M return {} with Dropped W input is not a JSON object") {
    constexpr std::string_view not_an_object = R"("just a string")";
    std::array<char, 5> small_buf{};
    const auto [value, status] = FitViewEventToBuffer(not_an_object, small_buf);
    REQUIRE(value == "{}");
    REQUIRE(status == Status::Dropped);
    REQUIRE(IsValidJson(value));
  }
}
