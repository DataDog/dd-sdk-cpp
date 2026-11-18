// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <type_traits>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/core/tracing/http_header_writer.hpp"

using datadog::HttpHeader;
using datadog::impl::HttpHeaderWriter;

static_assert(!std::is_default_constructible_v<HttpHeader>);
static_assert(!std::is_constructible_v<HttpHeader, std::string_view, std::string>);
static_assert(std::is_copy_constructible_v<HttpHeader>);

TEST_CASE("HttpHeader", "[unit][tracing][cpp-api]") {
  SECTION("M reference the original header name") {
    static constexpr std::string_view kHeaderName = "header-name";
    const auto header = HttpHeaderWriter::CreateHeader(kHeaderName, "header-value");

    REQUIRE(header.GetName().data() == kHeaderName.data());
  }

  SECTION("M retain a copied value after the original is destroyed") {
    const std::string expected(128, 'v');

    const auto copy = [&] {
      const auto original = HttpHeaderWriter::CreateHeader("header-name", expected);
      auto result = HttpHeader(original);

      REQUIRE(result.GetValue().data() != original.GetValue().data());

      return result;
    }();

    REQUIRE(copy.GetName() == "header-name");
    REQUIRE(copy.GetValue() == expected);
  }
}
