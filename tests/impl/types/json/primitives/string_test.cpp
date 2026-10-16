// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "support/json_serialization.hpp"

using namespace datadog::impl;

TEST_CASE("string JSON serialization", "[unit][json]") {
  SECTION("M render ordinary strings as quoted JSON string literals") {
    RequireJsonLiteral("", R"("")");
    RequireJsonLiteral("hello", R"("hello")");

    // Forward slash (a.k.a. "solidus" is not escaped)
    RequireJsonLiteral("/home/foo/bar", R"("/home/foo/bar")");

    // Single quotes are fine
    RequireJsonLiteral(
        R"(that's a nice face you got there...)",
        R"("that's a nice face you got there...")"
    );
  }

  SECTION("M escape quotes, slashes, and control codes") {
    RequireJsonLiteral(
        R"(that's a nice "face" you got there!)",
        R"("that's a nice \"face\" you got there!")"
    );
    RequireJsonLiteral(
        "\b for backspace, \f for feed, and you know \n \r and \t",
        R"("\b for backspace, \f for feed, and you know \n \r and \t")"
    );
    RequireJsonLiteral(
        "...\a, \a, \a went the trolley!",
        R"("...\u0007, \u0007, \u0007 went the trolley!")"
    );
    std::array<char, 4> bytes = {0x1e, 0x1f, 0x20, 0x21};
    RequireJsonLiteral(
        std::string_view(bytes.data(), bytes.size()), R"("\u001e\u001f !")"
    );
  }

  SECTION("M emit multi-byte UTF-8 chars unchanged") {
    RequireJsonLiteral(
        R"(хорошо, está bien, 私の牛が戻ってきた 🐮🕺🎉)",
        R"("хорошо, está bien, 私の牛が戻ってきた 🐮🕺🎉")"
    );
  }

  SECTION("M handle all common string types") {
    std::string s{"hello"};
    RequireJsonLiteral(s, "\"hello\"");
    RequireJsonLiteral(s.c_str(), "\"hello\"");

    std::array<char, 5> bytes = {0x68, 0x65, 0x6c, 0x6c, 0x6f};
    std::string_view sv(bytes.data(), bytes.size());
    RequireJsonLiteral(sv, "\"hello\"");
  }
}
