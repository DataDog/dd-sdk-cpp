// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/handlers/crashpad/view_event_fit.hpp"

#include <array>
#include <cstring>
#include <string_view>

#include "support/catch.hpp"

using namespace datadog::impl;

TEST_CASE("FitViewEventToBuffer", "[unit][crash_reporting]") {
  // Given two JSON payloads representing minimal view events, one with context and one
  // without
  static constexpr std::string_view TINY_EVENT =
      R"({"date":1,"_dd":{"format_version":2},"type":"view"})";
  static constexpr std::string_view TINY_EVENT_WITH_CONTEXT =
      R"({"date":1,"context":{"a":"1111111111","b":{"x":2222222222,"y":[true,false]},"c":"3333333333"},"_dd":{"format_version":2},"type":"view"})";

  SECTION("M encode OK and leave buffer unchanged W event fits buffer") {
    // Given an adequately sized buffer, both events encode OK
    std::array<char, 512> buf{};
    auto event = GENERATE(TINY_EVENT, TINY_EVENT_WITH_CONTEXT);
    REQUIRE(event.size() < buf.size());
    auto res = FitViewEventToBuffer(event, buf);
    REQUIRE(res.status == FitViewEventResult::Status::Ok);
    REQUIRE(res.value == event);

    // And the buffer remains zero-filled, i.e. we just returned a view to the input
    // event payload rather than copying it into the buffer
    REQUIRE(buf[0] == '\0');
  }

  SECTION("M encode OK W event fits buffer exactly") {
    // Given exactly-sized buffers for each event, the result is the same
    SECTION("{TINY_EVENT}") {
      std::array<char, TINY_EVENT.size()> buf{};
      auto res = FitViewEventToBuffer(TINY_EVENT, buf);
      REQUIRE(res.status == FitViewEventResult::Status::Ok);
      REQUIRE(res.value == TINY_EVENT);
    }
    SECTION("{TINY_EVENT_WITH_CONTEXT}") {
      std::array<char, TINY_EVENT_WITH_CONTEXT.size()> buf{};
      auto res = FitViewEventToBuffer(TINY_EVENT_WITH_CONTEXT, buf);
      REQUIRE(res.status == FitViewEventResult::Status::Ok);
      REQUIRE(res.value == TINY_EVENT_WITH_CONTEXT);
    }
  }

  SECTION("M fail to encode and write empty object W buffer is entirely too small") {
    // Given a buffer that will not fit TINY_EVENT as-is, neither event can be stored
    std::array<char, TINY_EVENT.size() - 1> buf{};
    auto event = GENERATE(TINY_EVENT, TINY_EVENT_WITH_CONTEXT);
    REQUIRE(event.size() > buf.size());
    auto res = FitViewEventToBuffer(event, buf);
    REQUIRE(res.status == FitViewEventResult::Status::Dropped);
    REQUIRE(res.value == "{}");
  }

  SECTION(
      "M progressively truncate top-level properties from back of context "
      "W buffer will not fit all custom attributes"
  ) {
    // Given the following 'context' object, TINY_EVENT_WITH_CONTEXT is 135 bytes:
    // `{"a":"1111111111","b":{"x":2222222222,"y":[true,false]},"c":"3333333333"}`
    REQUIRE(TINY_EVENT_WITH_CONTEXT.size() == 135);

    SECTION("{dropping context.c}") {
      // Given a buffer less than 135 bytes, we drop `"c":"3333333333"`, leaving "a" and
      // "b" in context
      std::array<char, 134> buf{};
      auto res = FitViewEventToBuffer(TINY_EVENT_WITH_CONTEXT, buf);
      REQUIRE(res.status == FitViewEventResult::Status::Truncated);
      REQUIRE(
          res.value ==
          R"({"date":1,"context":{"a":"1111111111","b":{"x":2222222222,"y":[true,false]}},"_dd":{"format_version":2},"type":"view"})"
      );
    }

    SECTION("{dropping context.b}") {
      // The result of {dropping context.c} above is 118 bytes; to fit anything smaller
      // we'll have to drop b as well (the entire object: we don't recurse into nested
      // subobjects and progressively drop their properties; we just drop from context)
      std::array<char, 117> buf{};
      auto res = FitViewEventToBuffer(TINY_EVENT_WITH_CONTEXT, buf);
      REQUIRE(res.status == FitViewEventResult::Status::Truncated);
      REQUIRE(
          res.value ==
          R"({"date":1,"context":{"a":"1111111111"},"_dd":{"format_version":2},"type":"view"})"
      );
    }

    SECTION("{dropping context.a}") {
      // The result of {dropping context.b} above is 80 bytes; to fit anything smaller
      // we'll have to drop a as well (we leave an empty "context" object rather than
      // stripping it entirely, as the extra logic required to remove the property and
      // any preceding or trailing commas is not worth saving 13 bytes)
      std::array<char, 79> buf{};
      auto res = FitViewEventToBuffer(TINY_EVENT_WITH_CONTEXT, buf);
      REQUIRE(res.status == FitViewEventResult::Status::Truncated);
      REQUIRE(
          res.value ==
          R"({"date":1,"context":{},"_dd":{"format_version":2},"type":"view"})"
      );
    }
  }
}
