// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/platform/http.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <charconv>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "datadog/impl/core/writer.hpp"

#include "support/http_server.hpp"

using namespace datadog;

// Tag [platform-http] describes tests used to validate that a platform-specific or
// user-provided implementation of platform/http.hpp behaves as expected

TEST_CASE("Http", "[unit][platform-http]") {
  SECTION("M create a valid IHttpSubsystem W static Init is called") {
    auto http_result = platform::Http::Init();
    REQUIRE(http_result.has_value());
    auto http = std::move(*http_result);
    REQUIRE(http != nullptr);
  }
}

TEST_CASE("IHttpSubsystem", "[unit][platform-http]") {
  // Given an HTTP subsystem
  auto http_result = platform::Http::Init();
  REQUIRE(http_result.has_value());
  auto http = std::move(*http_result);
  REQUIRE(http != nullptr);

  SECTION("M create a valid IHttpClient W ") {
    auto client = http->CreateClient();
    REQUIRE(client != nullptr);
  }
}

TEST_CASE("IHttpClient", "[unit][platform-http]") {
  // Given an HTTP client
  auto http_result = platform::Http::Init();
  REQUIRE(http_result.has_value());
  auto http = std::move(*http_result);
  REQUIRE(http != nullptr);
  auto client = http->CreateClient();
  REQUIRE(client != nullptr);

  SECTION("M get valid response W request is initiated") {
    // Given an HTTP server
    MockHttpServer server(0);
    server.Start();

    // And a simple request payload
    const std::string test_body = "mock request";
    impl::StringWriter body_writer{test_body};

    // When the client sends that payload
    const std::string url = server.BuildURL("/api/v1/test?foo=hello&bar=42");
    const std::string headers = "Content-Type: text/plain\n";
    auto result = client->Post(url.c_str(), headers.c_str(), body_writer);

    // Then it gets a valid response
    REQUIRE(result.type == platform::HttpResultType::GotResponse);
    REQUIRE(result.status_code == 200);

    // And the server receives the expected request
    server.Stop();
    REQUIRE(server.requests.size() == 1);
    const auto req = server.requests.front();
    auto req_pos = req.find("POST /api/v1/test?foo=hello&bar=42 HTTP/1.1\r\n");
    REQUIRE(req_pos == 0);
    auto header_pos = req.find("Content-Type: text/plain\r\n");
    REQUIRE(header_pos > req_pos);
    auto delim_pos = req.find("\r\n\r\n");
    REQUIRE(delim_pos > header_pos);
    auto body_text_pos = req.find("mock request");
    REQUIRE(body_text_pos > delim_pos);
  }

  SECTION("M propagate status code W server sends a response") {
    // Given an HTTP server
    MockHttpServer server(0);
    server.Start();

    // And a variety of status codes that the server can respond with
    std::vector<int> status_codes{200, 400, 429, 451, 500, 503};
    for (auto status_code : status_codes) {
      // When the client makes a request and gets a response
      server.SetResponseStatus(status_code);
      const std::string url = server.BuildURL("/test");
      auto result = client->Post(url.c_str(), "", impl::StringWriter{"hi"});

      // Then it dutifully conveys that status code
      REQUIRE(result.type == platform::HttpResultType::GotResponse);
      REQUIRE(result.status_code == status_code);
    }

    server.Stop();
    REQUIRE(server.requests.size() == status_codes.size());
  }

  SECTION("M signal retryable failure W server closes connection") {
    // Given an HTTP server that will close the connection without a response
    MockHttpServer server(0);
    server.close_after_read = true;
    server.Start();

    // When the client makes a request
    const std::string url = server.BuildURL("/test");
    auto start = std::chrono::high_resolution_clock::now();
    auto result = client->Post(url.c_str(), "", impl::StringWriter{"hi"});
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start
    );

    // Then the client will indicate a retryable error in response to reading EOF
    REQUIRE(result.type == platform::HttpResultType::GotNoResponse_Retryable);
    REQUIRE(result.status_code == 0);

    // And it will do so without delay
    REQUIRE(elapsed_ms.count() < 200);
  }

  SECTION("M signal non-retryable failure W request URL is invalid") {
    // When the client makes a malformed request
    auto result = client->Post("!@#$%^&", "", impl::StringWriter{"hi"});

    // Then the client will that it got no response, non-retryable
    REQUIRE(result.type == platform::HttpResultType::GotNoResponse_NonRetryable);
    REQUIRE(result.status_code == 0);
  }

  SECTION("M use chunked encoding W request is sent") {
    // Given an HTTP server
    MockHttpServer server(0);
    server.Start();

    // And a very large request body
    std::ostringstream oss;
    oss << "[{\"value\":\"0x0000\"}";
    for (int i = 1; i < 4096; i++) {
      oss << ",{\"value\":\"0x" << std::hex << std::setw(4) << std::setfill('0') << i
          << "\"}";
    }
    oss << "]";
    std::string s = oss.str();

    // When the client sends a request using that value
    const std::string url = server.BuildURL("/test");
    const std::string headers = "Content-Type: application/json\n";
    server.SetResponseStatus(202);
    auto result = client->Post(url.c_str(), headers.c_str(), impl::StringWriter{s});

    // Then the client will get an OK response
    REQUIRE(result.type == platform::HttpResultType::GotResponse);
    REQUIRE(result.status_code == 202);

    // And the server will have received the full payload
    server.Stop();
    REQUIRE(server.requests.size() == 1);
    const auto req = server.requests.front();
    REQUIRE(req.find("POST /test HTTP/1.1\r\n") != std::string::npos);
    REQUIRE(req.find("Transfer-Encoding: chunked\r\n") != std::string::npos);

    // And the request will have a body with length > what we sent
    const size_t blank_line_pos = req.find("\r\n\r\n");
    REQUIRE(blank_line_pos != std::string::npos);
    const size_t body_start = blank_line_pos + 4;
    REQUIRE(body_start > 4);
    const std::string req_body = req.substr(body_start);
    REQUIRE(req_body.size() > s.size());

    // And the body will be in valid chunked-encoding format
    const char* read_ptr = req_body.data();
    const char* read_end = read_ptr + req_body.size();
    std::string concatenated_chunks;
    concatenated_chunks.reserve(s.size());
    while (read_ptr < read_end) {
      // We should be looking at a hex-encoded size followed by CRLF
      const char* next_crlf = std::strstr(read_ptr, "\r\n");
      REQUIRE(next_crlf != nullptr);
      REQUIRE(next_crlf - read_ptr <= 8);

      // We should be able to read the chunk size from that hex string
      int chunk_size{0};
      auto from_chars_result = std::from_chars(read_ptr, next_crlf, chunk_size, 16);
      REQUIRE(from_chars_result.ec == std::errc());
      read_ptr = next_crlf + 2;

      // If chunk size is encoded at 0, final CRLF should signal end of body
      if (chunk_size == 0) {
        REQUIRE(read_ptr + 2 == read_end);
        REQUIRE(*read_ptr++ == '\r');
        REQUIRE(*read_ptr++ == '\n');
        break;
      }
      REQUIRE(read_ptr < read_end);

      // We should be looking at the data for the next chunk, followed by CRLF
      next_crlf = std::strstr(read_ptr, "\r\n");
      REQUIRE(next_crlf - read_ptr == chunk_size);
      REQUIRE(next_crlf + 2 < read_end);
      concatenated_chunks +=
          std::string_view{read_ptr, static_cast<size_t>(chunk_size)};
      read_ptr = next_crlf + 2;
    }

    // Concatenating the data for each chunk should give us our original data
    REQUIRE(concatenated_chunks == s);
  }
}
