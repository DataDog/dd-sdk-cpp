// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "datadog/impl/core/platform/http.hpp"

#include "mock/http_client.hpp"

using namespace datadog::platform;

TEST_CASE("MockHttpClient Default Behavior", "[unit][mock-http-client]") {
  MockHttpClient client;

  SECTION("M return default HTTP 200 response W no configuration") {
    // Given a default mock client with no configuration
    // When a POST request is made
    std::string test_body = "test data";
    size_t body_pos = 0;
    auto body_writer = [&test_body,
                        &body_pos](char* buffer, size_t num_bytes) -> size_t {
      if (body_pos >= test_body.length()) {
        return HTTP_WRITE_RESULT_EOF;
      }
      size_t to_copy = std::min(num_bytes, test_body.length() - body_pos);
      std::memcpy(buffer, test_body.c_str() + body_pos, to_copy);
      body_pos += to_copy;
      return to_copy;
    };

    auto result = client.Post(
        "https://api.datadoghq.com/test",
        "Content-Type: application/json\n",
        body_writer
    );

    // Then returns successful HTTP 200 response
    REQUIRE(result.type == HttpResultType::GotResponse);
    REQUIRE(result.status_code == 200);
  }

  SECTION("M record request details W POST is called") {
    // Given a mock client
    std::string test_url = "https://api.datadoghq.com/api/v1/logs";
    std::string test_headers = "Content-Type: application/json\nDD-API-KEY: secret\n";
    std::string test_body = "{\"message\":\"hello\"}";

    // When a POST request is made
    size_t body_pos = 0;
    auto body_writer = [&test_body,
                        &body_pos](char* buffer, size_t num_bytes) -> size_t {
      if (body_pos >= test_body.length()) {
        return HTTP_WRITE_RESULT_EOF;
      }
      size_t to_copy = std::min(num_bytes, test_body.length() - body_pos);
      std::memcpy(buffer, test_body.c_str() + body_pos, to_copy);
      body_pos += to_copy;
      return to_copy;
    };

    client.Post(test_url.c_str(), test_headers.c_str(), body_writer);

    // Then request details are recorded
    REQUIRE(client.requests.size() == 1);
    const auto& request = client.requests[0];
    REQUIRE(request.url == test_url);
    REQUIRE(request.headers == test_headers);
    REQUIRE(request.body == test_body);
    REQUIRE(request.aborted == false);
  }

  SECTION("M record multiple requests W POST is called multiple times") {
    // Given a mock client
    std::string body1 = "first";
    std::string body2 = "second";

    // When multiple POST requests are made
    auto make_writer = [](const std::string& body) {
      return [body](char* buffer, size_t num_bytes) -> size_t {
        static thread_local size_t pos = 0;
        if (pos >= body.length()) {
          pos = 0;  // Reset for next call
          return HTTP_WRITE_RESULT_EOF;
        }
        size_t to_copy = std::min(num_bytes, body.length() - pos);
        std::memcpy(buffer, body.c_str() + pos, to_copy);
        pos += to_copy;
        return to_copy;
      };
    };

    client.Post("url1", "headers1", make_writer(body1));
    client.Post("url2", "headers2", make_writer(body2));

    // Then both requests are recorded
    REQUIRE(client.requests.size() == 2);
    REQUIRE(client.requests[0].url == "url1");
    REQUIRE(client.requests[0].body == body1);
    REQUIRE(client.requests[1].url == "url2");
    REQUIRE(client.requests[1].body == body2);
  }
}

TEST_CASE("MockHttpClient Response Simulation", "[unit][mock-http-client]") {
  MockHttpClient client;
  auto dummy_writer = [](char*, size_t) -> size_t { return HTTP_WRITE_RESULT_EOF; };

  SECTION("M return configured HTTP status code W SimulateResponse is called") {
    // Given a mock client configured for HTTP 404
    client.SimulateResponse(404);

    // When a POST request is made
    auto result = client.Post("test-url", "test-headers", dummy_writer);

    // Then returns HTTP 404 response
    REQUIRE(result.type == HttpResultType::GotResponse);
    REQUIRE(result.status_code == 404);
  }

  SECTION(
      "M return transient network error W SimulateTransientNetworkError is called"
  ) {
    // Given a mock client configured for transient error
    client.SimulateTransientNetworkError();

    // When a POST request is made
    auto result = client.Post("test-url", "test-headers", dummy_writer);

    // Then returns retryable network error
    REQUIRE(result.type == HttpResultType::GotNoResponse_Retryable);
    REQUIRE(result.status_code == 0);
  }

  SECTION("M return bad request error W SimulateBadRequest is called") {
    // Given a mock client configured for bad request
    client.SimulateBadRequest();

    // When a POST request is made
    auto result = client.Post("test-url", "test-headers", dummy_writer);

    // Then returns non-retryable error
    REQUIRE(result.type == HttpResultType::GotNoResponse_NonRetryable);
    REQUIRE(result.status_code == 0);
  }

  SECTION("M persist configuration across multiple requests W response is configured") {
    // Given a mock client configured for HTTP 500
    client.SimulateResponse(500);

    // When multiple POST requests are made
    auto result1 = client.Post("test-url1", "headers", dummy_writer);
    auto result2 = client.Post("test-url2", "headers", dummy_writer);

    // Then both return the configured response
    REQUIRE(result1.type == HttpResultType::GotResponse);
    REQUIRE(result1.status_code == 500);
    REQUIRE(result2.type == HttpResultType::GotResponse);
    REQUIRE(result2.status_code == 500);
  }

  SECTION("M override previous configuration W multiple simulation methods called") {
    // Given a mock client configured for different responses
    client.SimulateResponse(200);
    client.SimulateTransientNetworkError();  // Override previous

    // When a POST request is made
    auto result = client.Post("test-url", "headers", dummy_writer);

    // Then returns the latest configuration
    REQUIRE(result.type == HttpResultType::GotNoResponse_Retryable);
    REQUIRE(result.status_code == 0);
  }
}

TEST_CASE("MockHttpClient Body Streaming", "[unit][mock-http-client]") {
  MockHttpClient client;

  SECTION("M read body in multiple chunks W body writer returns partial data") {
    // Given a body that will be streamed in chunks
    std::string full_body =
        "This is a long message that will be streamed in multiple chunks";
    size_t chunk_size = 10;
    size_t body_pos = 0;

    auto chunked_writer =
        [&full_body, &body_pos, chunk_size](char* buffer, size_t num_bytes) -> size_t {
      if (body_pos >= full_body.length()) {
        return HTTP_WRITE_RESULT_EOF;
      }
      size_t to_copy = std::min({num_bytes, chunk_size, full_body.length() - body_pos});
      std::memcpy(buffer, full_body.c_str() + body_pos, to_copy);
      body_pos += to_copy;
      return to_copy;
    };

    // When POST is called with chunked writer
    client.Post("test-url", "test-headers", chunked_writer);

    // Then full body is reconstructed from chunks
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == full_body);
    REQUIRE(client.requests[0].aborted == false);
  }

  SECTION("M handle empty body W writer immediately returns EOF") {
    // Given a writer that returns no data
    auto empty_writer = [](char*, size_t) -> size_t { return HTTP_WRITE_RESULT_EOF; };

    // When POST is called
    client.Post("test-url", "test-headers", empty_writer);

    // Then empty body is recorded
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body.empty());
    REQUIRE(client.requests[0].aborted == false);
  }

  SECTION("M handle aborted request W writer returns ABORT") {
    // Given a writer that aborts after some data
    std::string partial_body = "partial";
    bool should_abort = false;

    auto aborting_writer = [&partial_body,
                            &should_abort](char* buffer, size_t) -> size_t {
      if (should_abort) {
        return HTTP_WRITE_RESULT_ABORT;
      }
      std::memcpy(buffer, partial_body.c_str(), partial_body.length());
      should_abort = true;
      return partial_body.length();
    };

    // When POST is called with aborting writer
    client.Post("test-url", "test-headers", aborting_writer);

    // Then partial body and abort flag are recorded
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == partial_body);
    REQUIRE(client.requests[0].aborted == true);
  }

  SECTION("M handle immediate abort W writer returns ABORT on first call") {
    // Given a writer that immediately aborts
    auto immediate_abort_writer = [](char*, size_t) -> size_t {
      return HTTP_WRITE_RESULT_ABORT;
    };

    // When POST is called
    client.Post("test-url", "test-headers", immediate_abort_writer);

    // Then abort is recorded with empty body
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body.empty());
    REQUIRE(client.requests[0].aborted == true);
  }
}

TEST_CASE("MockHttpSubsystem", "[unit][mock-http-client]") {
  SECTION("M create MockHttpClient instance W CreateClient is called") {
    // Given a mock HTTP subsystem
    MockHttpSubsystem subsystem;

    // When CreateClient is called
    auto client = subsystem.CreateClient();

    // Then returns a valid MockHttpClient instance
    REQUIRE(client != nullptr);

    // And: Client can be cast to MockHttpClient for test access
    auto* mock_client = dynamic_cast<MockHttpClient*>(client.get());
    REQUIRE(mock_client != nullptr);
    REQUIRE(mock_client->requests.empty());
  }

  SECTION("M create independent clients W CreateClient called multiple times") {
    // Given a mock HTTP subsystem
    MockHttpSubsystem subsystem;

    // When multiple clients are created
    auto client1 = subsystem.CreateClient();
    auto client2 = subsystem.CreateClient();

    // Then each client is independent
    REQUIRE(client1 != nullptr);
    REQUIRE(client2 != nullptr);
    REQUIRE(client1.get() != client2.get());

    // And: Each has its own request history
    auto* mock1 = dynamic_cast<MockHttpClient*>(client1.get());
    auto* mock2 = dynamic_cast<MockHttpClient*>(client2.get());
    REQUIRE(mock1 != nullptr);
    REQUIRE(mock2 != nullptr);

    auto dummy_writer = [](char*, size_t) -> size_t { return HTTP_WRITE_RESULT_EOF; };
    mock1->Post("url1", "headers", dummy_writer);

    REQUIRE(mock1->requests.size() == 1);
    REQUIRE(mock2->requests.size() == 0);
  }
}