// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <vector>

#include "datadog/impl/core/http/client.hpp"

using namespace datadog;

/**
 * Details of a request made by MockHttpClient.
 */
struct MockHttpRequest {
  std::string url;
  std::string headers;
  std::string body;
  bool aborted{false};

  bool MatchesPath(std::string_view path) const {
    // Strip trailing "?..." if present
    std::string_view url_sans_query = url;
    if (auto pos = url_sans_query.find('?'); pos != std::string_view::npos) {
      url_sans_query = url_sans_query.substr(0, pos);
    }

    // Take the last path.size() characters from the URL and perform a string equality
    // check
    if (url_sans_query.size() < path.size()) {
      return false;
    }
    std::string_view url_suffix =
        url_sans_query.substr(url_sans_query.size() - path.size(), path.size());
    return url_suffix == path;
  }
};

/**
 * Mock implementation of IHttpClient.
 */
class MockHttpClient : public impl::IHttpClient {
 public:
  // Details of calls to Post will be recorded here
  std::vector<MockHttpRequest> requests;

  // Tests can set this value after ctor to determine the result that that client will
  // receive; or use the helper functions below
  impl::HttpResult result{impl::HttpResultType::GotResponse, 200};

 public:
  /**
   * Subsequent calls to Post() will result in an HTTP response with this status code.
   */
  void SimulateResponse(int status_code) {
    result.type = impl::HttpResultType::GotResponse;
    result.status_code = status_code;
  }

  /**
   * Subsequent calls to Post() will result in GotNoResponse_Retryable.
   */
  void SimulateTransientNetworkError() {
    result.type = impl::HttpResultType::GotNoResponse_Retryable;
    result.status_code = 0;
  }

  /**
   * Subsequent calls to Post() will result in GotNoResponse_NonRetryable.
   */
  void SimulateBadRequest() {
    result.type = impl::HttpResultType::GotNoResponse_NonRetryable;
    result.status_code = 0;
  }

  /**
   * Simulates a request, reading the entire body from body_writer, then copying the
   * request details into the requests vector for later examination. Returns the mock
   * result value last configured, or HTTP 200 by default.
   */
  virtual impl::HttpResult Post(
      const char* url, const char* headers, impl::HttpBodyWriter body_writer
  ) override {
    // Record the details of this request
    MockHttpRequest request;
    request.url = std::string(url);
    request.headers = std::string(headers);

    // Stream request body via writer func
    char buffer[1024];
    while (true) {
      const size_t num_bytes_read = body_writer(buffer, sizeof(buffer));
      if (num_bytes_read == impl::HTTP_WRITE_RESULT_ABORT) {
        request.aborted = true;
        break;
      }
      if (num_bytes_read == 0) {
        break;
      }
      request.body += std::string_view{buffer, num_bytes_read};
    }

    // Record the request
    requests.emplace_back(std::move(request));

    // Return predetermined result value
    return result;
  }
};

/**
 * Mock implementation of IHttpSubsystem; creates MockHttpClient;
 */
class MockHttpSubsystem : public impl::IHttpSubsystem {
 public:
  // Non-owning pointers to any clients created via CreateClient(): pointers become
  // invalid when clients are destroyed; this simply allows test code to grab a
  // reference to the actual HTTP client(s) used by the core
  std::vector<MockHttpClient*> clients;

  std::string_view GetName() const override { return "mock"; }

  std::string_view GetVersion() const override { return "0.0.0"; }

  std::unique_ptr<impl::IHttpClient> CreateClient() override {
    auto client = std::make_unique<MockHttpClient>();
    clients.push_back(client.get());
    return client;
  }
};
