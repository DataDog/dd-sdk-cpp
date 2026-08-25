// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/core/http/body_writer.hpp"
#include "datadog/impl/core/http/result.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * An HTTP client that can send requests to a remote endpoint, having no more than a
 * single request in flight at any given time.
 */
class IHttpClient {
 protected:
  IHttpClient() = default;

 public:
  virtual ~IHttpClient() = default;

  // An IHttpClient is never copied or moved
  IHttpClient(const IHttpClient&) = delete;
  IHttpClient& operator=(const IHttpClient&) = delete;
  IHttpClient(IHttpClient&&) = delete;
  IHttpClient& operator=(IHttpClient&&) = delete;

  /**
   * Sends a POST request to the given HTTP endpoint, blocking until finished.
   *
   * To minimize copying, HTTP requests are made with 'Transfer-Encoding: chunked',
   * allowing payloads to be streamed directly to the TCP socket as they're read.
   *
   * @param url The fully-qualified URL, including origin. Any query parameters will be
   *  present in the URL, properly URL-encoded.
   * @param headers The full set of request headers, in wire format, i.e.
   *  'Content-Type: application/json', delimited by '\n', with a trailing newline.
   *  TODO whoops, wire format actually uses '\r\n' - curl does the right thing, but
   *  the application currently expects the nonstandard '\n', so we should fix this
   *  and make sure comments are clear about the expected behavior.
   * @param body_writer A function that will populate the body of the request,
   *  chunk-by-chunk, allowing payloads to be streamed from the application layer to
   *  the HTTP connection. Note that this function will only be called during the
   *  blocking call to `Post()`, so it's generally safe for it to reference temporary
   *  values available from the stack frame where `Post()` is called.
   */
  virtual HttpResult Post(
      const char* url, const char* headers, HttpBodyWriter body_writer
  ) = 0;
};

/**
 * Interface to the HTTP client implementation used on the current platform.
 */
class IHttpSubsystem {
 protected:
  IHttpSubsystem() = default;

 public:
  virtual ~IHttpSubsystem() = default;

  // The IHttpSubsystem is never copied or moved
  IHttpSubsystem(const IHttpSubsystem&) = delete;
  IHttpSubsystem& operator=(const IHttpSubsystem&) = delete;
  IHttpSubsystem(IHttpSubsystem&&) = delete;
  IHttpSubsystem& operator=(IHttpSubsystem&&) = delete;

  virtual std::string_view GetName() const = 0;
  virtual std::string_view GetVersion() const = 0;
  virtual std::unique_ptr<IHttpClient> CreateClient() = 0;
};

/**
 * Defines the function `datadog::impl::Http::Init()`, which is used in production
 * code to initialize the HTTP client implementation used in the current build.
 */
namespace Http {
std::unique_ptr<IHttpSubsystem> Init(const DiagnosticLogger& logger);
};  // namespace Http

}  // namespace datadog::impl
