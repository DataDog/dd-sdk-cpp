// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

/**
 * Default HTTP client implementation using libcurl.
 */
#include "datadog/impl/assert.hpp"
#include "datadog/impl/platform/http.hpp"

#include "curl/curl.h"

namespace datadog::platform {

/**
 * Callback passed via CURLOPT_READFUNCTION in order to populate the body of an HTTP
 * request. Wraps an HttpBodyWriter function.
 *
 * @param buffer The address to which the next chunk of the request body should be
 *  written.
 * @param size The size of an "item" (i.e. byte) in that buffer; always 1.
 * @param nitems The number of "items" to write: `size * nitems` represents the maxmium
 *  number of bytes that may be written to `buffer`.
 * @param userdata The value provided via CURLOPT_READDATA; must be the address of an
 *  HttpBodyWriter.
 *
 * @returns The number of bytes actually written to the buffer, with 0 signalling EOF,
 *  or CURL_READFUNC_ABORT to signal an error.
 */
static size_t read_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
  // We should never be called without a valid userdata pointer
  DATADOG_ASSERT(userdata, "CURLOPT_READFUNCTION set without valid CURLOPT_READDATA");

  // userdata should always point to an HttpBodyWriter function
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  HttpBodyWriter* body_writer = (reinterpret_cast<HttpBodyWriter*>(userdata));

  // Defer to the body reader to read the next chunk and write it into the buffer
  // representing the request body
  const size_t num_bytes = size * nitems;
  const size_t result = (*body_writer)(buffer, num_bytes);

  // If the reader signaled an error, abort; otherwise return the number of bytes
  // written to buffer
  if (result == HTTP_WRITE_RESULT_ABORT) {
    return CURL_READFUNC_ABORT;
  }
  return result;
}

static curl_slist* build_slist(std::string_view headers) {
  // If our headers string is empty, return null to indicate no custom headers
  curl_slist* slist = nullptr;
  if (headers.empty()) {
    return slist;
  }

  // Headers must be given to us with a trailing newline
  DATADOG_ASSERT(headers.back() == '\n', "HTTP headers missing trailing newline");

  // Create a copy of the string that contains our newline-delimited header values, so
  // we can mutate it in-place to make null-terminated strings to pass into curl
  std::string s(headers);
  char* buffer = s.data();

  // Iterate over buffer line-by-line, writing a zero byte over each newline we find,
  // and passing the resulting C string value into curl
  char* line_start = buffer;
  char* current = buffer;
  const char* buffer_end = buffer + s.size();
  while (current < buffer_end) {
    if (*current == '\n') {
      *current = '\0';
      slist = curl_slist_append(slist, line_start);
      line_start = current + 1;
    }
    current++;
  }

  // Append an empty 'Expect' header to override libcurl's use of
  // 'Expect: 100-continue' with chunked encoding
  slist = curl_slist_append(slist, "Expect:");

  // Return the final list
  return slist;
}

// NOLINTNEXTLINE(readability-non-const-parameter)
static size_t write_callback(char* data, size_t size, size_t nmemb, void* userdata) {
  // Just consume the response and ignore it
  (void)data;
  (void)userdata;
  return size * nmemb;
}

class CurlHttpClient final : public IHttpClient {
 private:
  CURL* _curl;

 public:
  explicit CurlHttpClient(CURL* curl) : _curl(curl) {
    DATADOG_ASSERT(_curl, "CurlHttpClient constructed with null curl handle");
  }

  ~CurlHttpClient() override { curl_easy_cleanup(_curl); }

  // An IHttpClient is never copied or moved
  CurlHttpClient(const CurlHttpClient&) = delete;
  CurlHttpClient& operator=(const CurlHttpClient&) = delete;
  CurlHttpClient(CurlHttpClient&&) = delete;
  CurlHttpClient& operator=(CurlHttpClient&&) = delete;

  HttpResult Post(
      const char* url, const char* headers, HttpBodyWriter body_writer
  ) override {
    // Store the error code from our most recent curl API call
    CURLcode res{CURL_LAST};

    // Reset our handle: we only make one request at a time in any given HttpClient
    curl_easy_reset(_curl);

    // Set the request URL
    res = curl_easy_setopt(_curl, CURLOPT_URL, url);
    if (res != CURLE_OK) {
      // Failing to set the URL likely means that we were given a malformed URL,
      // which implies a bug in the reporter implementation: this is fatal
      return HttpResult{HttpResultType::SentNoRequest, 0};
    }

    // Build a curl-compatible linked list specifying our request headers
    // TODO: Usually, the same set of headers is used across subsequent requests, so we
    // could avoid allocating on every request by caching a small set of curl_slist*
    // values, indexed by the string headers used to construct them.
    curl_slist* headers_slist = build_slist(headers);
    res = curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, headers_slist);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_HTTPHEADER");

    // Point curl to our callback function that will read the next chunk of data into
    // the request body when ready
    res = curl_easy_setopt(_curl, CURLOPT_READFUNCTION, read_callback);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_READFUNCTION");

    // Supply the address of the body_writer function as the void* parameter to our read
    // function: it's safe for our callback to reinterpret_cast and call the function
    // because the request occurs synchronously within this stack frame
    res = curl_easy_setopt(_curl, CURLOPT_READDATA, &body_writer);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_READDATA");

    // Set CURLOPT_UPLOAD, which tells curl to activate the read callback mechanism
    res = curl_easy_setopt(_curl, CURLOPT_UPLOAD, 1L);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_UPLOAD");

    // Specify -1 as the size of our uploaded data: this ensures that curl will not set
    // a Content-Length header, allowing us to stream the request body with
    // `Transfer-Encoding: chunked` without needing to precompute its size
    const curl_off_t fieldsize_none = -1;
    res = curl_easy_setopt(_curl, CURLOPT_POSTFIELDSIZE_LARGE, fieldsize_none);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_POSTFIELDSIZE_LARGE");

    // Set the request method to POST: for historical reasons, CURLOPT_UPLOAD sets the
    // method to PUT, so we need to explicitly override it to POST
    res = curl_easy_setopt(_curl, CURLOPT_POST, 1L);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_POST");

    // Supply a read callback to ensure that curl will wait until it's read the response
    // body to close the connection
    res = curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, write_callback);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_WRITEFUNCTION");

    // Configure timeouts to prevent hanging: 10s to connect, 30s total
    res = curl_easy_setopt(_curl, CURLOPT_CONNECTTIMEOUT, 10L);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_CONNECTTIMEOUT");
    res = curl_easy_setopt(_curl, CURLOPT_TIMEOUT, 30L);
    DATADOG_ASSERT(res == CURLE_OK, "Failed to set CURLOPT_TIMEOUT");

    // Initiate the request and block until it's finished
    const CURLcode perform_res = curl_easy_perform(_curl);

    // Interpret the result
    HttpResultType result_type = HttpResultType::SentNoRequest;
    int status_code = 0;
    switch (perform_res) {
      // If our request completed successfully, get the response code
      case CURLE_OK:
        res = curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &status_code);
        DATADOG_ASSERT(
            res == CURLE_OK,
            "Failed to get CURLINFO_RESPONSE_CODE after curl_easy_perform "
            "returned OK"
        );
        result_type = HttpResultType::GotResponse;
        break;

      // These network-related errors may be temporary; allow retrying
      case CURLE_COULDNT_RESOLVE_PROXY:
      case CURLE_COULDNT_RESOLVE_HOST:
      case CURLE_COULDNT_CONNECT:
      case CURLE_WEIRD_SERVER_REPLY:
      case CURLE_REMOTE_ACCESS_DENIED:
      case CURLE_HTTP2:
      case CURLE_PARTIAL_FILE:
      case CURLE_UPLOAD_FAILED:
      case CURLE_OPERATION_TIMEDOUT:
      case CURLE_SSL_CONNECT_ERROR:
      case CURLE_TOO_MANY_REDIRECTS:
      case CURLE_GOT_NOTHING:
      case CURLE_SEND_ERROR:
      case CURLE_RECV_ERROR:
      case CURLE_PEER_FAILED_VERIFICATION:
      case CURLE_HTTP2_STREAM:
      case CURLE_PROXY:
      case CURLE_HTTP3:
        result_type = HttpResultType::GotNoResponse_Retryable;
        break;

      // For all other errors, consider the request fundamentally invalid
      default:
        result_type = HttpResultType::GotNoResponse_NonRetryable;
    }

    // Free any memory allocated to pass our headers to curl
    // TODO: Remove this call if we cache header slists for reuse
    curl_slist_free_all(headers_slist);

    // Return our result
    return HttpResult{result_type, status_code};
  }
};

class CurlHttpSubsystem final : public IHttpSubsystem {
 public:
  CurlHttpSubsystem() = default;

  ~CurlHttpSubsystem() override {
    // Tear down curl on SDK shutdown
    curl_global_cleanup();
  }

  // The IHttpSubsystem is never copied or moved
  CurlHttpSubsystem(const CurlHttpSubsystem&) = delete;
  CurlHttpSubsystem& operator=(const CurlHttpSubsystem&) = delete;
  CurlHttpSubsystem(CurlHttpSubsystem&&) = delete;
  CurlHttpSubsystem& operator=(CurlHttpSubsystem&&) = delete;

  std::unique_ptr<IHttpClient> CreateClient() override {
    // Initialize the curl handle that our client implementation will use to make
    // requests: clients can only make one request at a time
    CURL* curl = curl_easy_init();
    if (!curl) {
      return nullptr;
    }

    // Initialize our client once we're guaranteed a valid curl handle
    return std::make_unique<CurlHttpClient>(curl);
  }
};

Http::InitResult Http::Init() {
  // Initialize curl: if successful, return an HttpSubsystem implementation
  CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
  if (res == CURLE_OK) {
    // When the application eventually releases this unique_ptr, curl_global_cleanup
    // will be called via ~CurlHttpSubsystem()
    return std::make_unique<CurlHttpSubsystem>();
  }

  // If curl initialization fails, attempt to gather diagnostic info
  Attribute curl_version = Attribute::String("n/a");
  Attribute ssl_version = Attribute::String("n/a");
  Attribute zlib_version = Attribute::String("n/a");
  curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
  if (info) {
    if (info->version) {
      curl_version.SetString(info->version);
    }
    if (info->ssl_version) {
      ssl_version.SetString(info->ssl_version);
    }
    if (info->libz_version) {
      zlib_version.SetString(info->libz_version);
    }
  }

  // Return an error that wraps the human-readable libcurl error message and includes
  // diagnostic info in custom attributes
  const char* curl_error_text = curl_easy_strerror(res);
  return nonstd::make_unexpected(
      datadog::impl::ErrorMessage(
          curl_error_text,
          {{"curl_version", curl_version},
           {"ssl_version", ssl_version},
           {"zlib_version", zlib_version}}
      ).AddPrefix("libcurl init failed")
  );
}

}  // namespace datadog::platform
