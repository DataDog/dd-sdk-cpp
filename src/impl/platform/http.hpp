/**
 * Type definitions for the HTTP client functionality used by the Datadog SDK.
 *
 * As currently implemented, the SDK's concurrency model for HTTP requests favors
 * clarity and simplicity.  The SDK has at most one HTTP request in flight at any given
 * time, and the core maintains a single HTTP client that is used for all requests.
 */
#pragma once

#include <cinttypes>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace datadog::platform {

enum class HttpResultType : uint8_t {
  /**
   * We were unable to initiate the request due to an internal error in our HTTP
   * implementation (e.g. failed allocation, bad invariants), indicating a fatal error
   * state.
   */
  SentNoRequest,
  /**
   * We failed to complete the request, and the request should be discarded and never
   * retried.
   */
  GotNoResponse_NonRetryable,
  /**
   * We failed to complete the request due to transient network conditions, but the same
   * request may succeed if retried later.
   */
  GotNoResponse_Retryable,
  /**
   * We completed the request and got a valid response, and status_code is set.
   */
  GotResponse,
};

/**
 * Result of an attempt to send an HTTP request. status_code is only set if type is
 * GotResponse.
 */
struct HttpResult {
  HttpResultType type;
  int status_code;
};

/**
 * Reads up to num_bytes bytes of data from the payload representing the body of an
 * outgoing HTTP request, then writes it into buffer.
 *
 * @return The number of bytes written to buffer. If HTTP_WRITE_RESULT_EOF, no bytes
 *  were written, there is no more data to write, and the request should be finished
 *  successfully. If HTTP_WRITE_RESULT_ABORT, writing can not proceed due to an error,
 *  and the request should be aborted.
 */
using HttpBodyWriter = std::function<size_t(char* buffer, size_t num_bytes)>;
static const size_t HTTP_WRITE_RESULT_EOF = 0;
static const size_t HTTP_WRITE_RESULT_ABORT = 0xffffffff;

/**
 * Interface to an HTTP client.
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

  virtual std::unique_ptr<IHttpClient> CreateClient() = 0;
};

namespace Http {
std::unique_ptr<IHttpSubsystem> Init();
};

}  // namespace datadog::platform
