// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "events/timestamp.hpp"

#include <cinttypes>

#include "events/enum.hpp"
#include "support/catch.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

static const Timestamp Y2K_EVE{std::chrono::nanoseconds(946684799999999999)};

TEST_CASE("timestamp JSON serialization", "[unit][events]") {
  SECTION("M render ISO-8601 W value type is IsoTimestamp") {
    IsoTimestamp value;
    value = Y2K_EVE;
    RequireJsonValue(value, "\"1999-12-31T23:59:59.999Z\"");
  }
  SECTION("M render integer nanoseconds W value type is NanoTimestamp") {
    NanoTimestamp value;
    value = Y2K_EVE;
    RequireJsonValue(value, "946684799999999999");
  }
  SECTION("M render integer milliseconds W value type is MilliTimestamp") {
    MilliTimestamp value;
    value = Y2K_EVE;
    RequireJsonValue(value, "946684799999");
  }
}

struct AggregateTimestamps {
  IsoTimestamp iso;
  NanoTimestamp nano;
  MilliTimestamp milli;
};

TEST_CASE("timestamp construction", "[unit][events]") {
  SECTION("M default-initialize to zero") {
    AggregateTimestamps agg;
    RequireJsonValue(agg.iso, "\"1970-01-01T00:00:00.000Z\"");
    RequireJsonValue(agg.nano, "0");
    RequireJsonValue(agg.milli, "0");
  }

  SECTION("M support construction from datadog::Timestamp") {
    AggregateTimestamps agg{Y2K_EVE, Y2K_EVE, Y2K_EVE};
    RequireJsonValue(agg.iso, "\"1999-12-31T23:59:59.999Z\"");
    RequireJsonValue(agg.nano, "946684799999999999");
    RequireJsonValue(agg.milli, "946684799999");
  }
}
