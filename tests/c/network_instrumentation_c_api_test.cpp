// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "datadog/network_instrumentation.h"
#include "datadog/network_instrumentation.hpp"

#include "datadog/c/network_instrumentation_glue.hpp"
#include "datadog/impl/core/tracing/http_header_writer.hpp"

using datadog::HttpHeader;
using datadog::TraceInjectionPolicy;
using datadog::impl::HttpHeaderWriter;

static_assert(
    static_cast<int>(TraceInjectionPolicy::All) == DD_TRACE_INJECTION_POLICY_ALL
);
static_assert(
    static_cast<int>(TraceInjectionPolicy::Sampled) == DD_TRACE_INJECTION_POLICY_SAMPLED
);

using HeadersPtr =
    std::unique_ptr<dd_http_headers_t, decltype(&dd_http_headers_destroy)>;

static HeadersPtr make_headers(std::vector<HttpHeader> headers) {
  auto owner = std::make_unique<dd_http_headers>(std::move(headers));
  return HeadersPtr(owner.release(), dd_http_headers_destroy);
}

TEST_CASE("dd_http_headers", "[unit][tracing][c-api]") {
  SECTION("M reference the original header name") {
    static constexpr std::string_view kHeaderName = "header-name";
    const auto headers =
        make_headers({HttpHeaderWriter::CreateHeader(kHeaderName, "header-value")});
    dd_http_header_t header{};

    const bool success = dd_http_headers_get(headers.get(), 0, &header);

    REQUIRE(success);
    REQUIRE(header.name == kHeaderName.data());
  }

  SECTION("M retain a copied value after the original is destroyed") {
    const std::string expected(128, 'v');
    const auto headers = [&] {
      const auto original = HttpHeaderWriter::CreateHeader("header-name", expected);
      return make_headers({original});
    }();
    dd_http_header_t header{};

    const bool success = dd_http_headers_get(headers.get(), 0, &header);

    REQUIRE(success);
    REQUIRE(std::string_view(header.value) == expected);
  }
}
