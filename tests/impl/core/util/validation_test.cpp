// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/util/validation.hpp"

#include <limits>

#include "support/catch.hpp"

using namespace datadog::impl;

TEST_CASE("IsBlankString", "[unit][validation]") {
  SECTION("M return true W input value is empty") {
    REQUIRE(IsBlankString(std::string_view{}) == true);
  }
  SECTION("M return true W input value is all spaces") {
    REQUIRE(IsBlankString("   ") == true);
  }
  SECTION("M return true W input value is all tabs") {
    REQUIRE(IsBlankString("\t\t\t") == true);
  }
  SECTION("M return true W input value is all newlines") {
    REQUIRE(IsBlankString("\n\n\n") == true);
  }
  SECTION("M return true W input value is all CRLF") {
    REQUIRE(IsBlankString("\r\n\r\n\r\n") == true);
  }
  SECTION("M return false W input value is non-whitespace") {
    REQUIRE(IsBlankString("hello") == false);
  }
  SECTION("M return false W input value is mixed whitespace and non-whitespace") {
    REQUIRE(IsBlankString(" \t hello \n ") == false);
  }
}

TEST_CASE("IsCBlankString", "[unit][validation]") {
  SECTION("M return true W input value is NULL") {
    REQUIRE(IsBlankCString(nullptr) == true);
  }
  SECTION("M return true W input value is whitespace") {
    REQUIRE(IsBlankCString("  \t  ") == true);
  }
  SECTION("M return false W input value contains any non-whitespace") {
    REQUIRE(IsBlankCString("  \t  foo") == false);
  }
}

TEST_CASE("IsValidLongTaskDurationSeconds", "[unit][validation]") {
  SECTION("M return true W input value is a typical positive duration") {
    REQUIRE(IsValidLongTaskDurationSeconds(0.005) == true);
  }
  SECTION("M return false W input value is zero") {
    REQUIRE(IsValidLongTaskDurationSeconds(0) == false);
  }
  SECTION("M return false W input value is negative") {
    REQUIRE(IsValidLongTaskDurationSeconds(-0.005) == false);
  }
  SECTION("M return false W input value is NaN") {
    REQUIRE(
        IsValidLongTaskDurationSeconds(std::numeric_limits<double>::quiet_NaN()) ==
        false
    );
  }
  SECTION("M return false W input value is positive infinity") {
    REQUIRE(
        IsValidLongTaskDurationSeconds(std::numeric_limits<double>::infinity()) == false
    );
  }
  SECTION("M return false W input value is negative infinity") {
    REQUIRE(
        IsValidLongTaskDurationSeconds(-std::numeric_limits<double>::infinity()) ==
        false
    );
  }
  SECTION("M return false W input value overflows a nanosecond count") {
    REQUIRE(
        IsValidLongTaskDurationSeconds(std::numeric_limits<double>::max()) == false
    );
  }
}
