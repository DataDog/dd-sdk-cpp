// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/util/json.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <string_view>
#include <vector>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/events/struct.hpp"

namespace datadog::impl {
struct JsonTestEvent {
  UUID id;
  uint64_t foo{0};
  std::string bar;
};
DATADOG_JSON_STRUCT(
    JsonTestEvent,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(foo),
    DATADOG_JSON_FIELD(bar)
)
};  // namespace datadog::impl

struct JsonBuffer {
  std::vector<uint8_t> bytes;
  std::string_view ToString() const {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
};

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("EncodeJson", "[unit][json]") {
  // Given a statically-defined event payload and a buffer to write it to
  JsonTestEvent ev{*UUID::Parse("56c031d0-24e3-4fb3-bba5-8ac53b4041d1"), 42, "hi"};
  JsonBuffer buf;

  SECTION("M write value to buffer as JSON") {
    // When we call EncodeJson
    EncodeJson(buf.bytes, ev);

    // Then the buffer contains the details of our event, serialized as a JSON object
    REQUIRE(
        buf.ToString() ==
        R"({"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi"})"
    );
  }
}
