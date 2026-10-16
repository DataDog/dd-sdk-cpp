// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// clang-format off
#include "datadog/impl/types/windows_headers.hpp"
#include <winhttp.h>  // Must follow windows.h
// clang-format on

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "datadog/impl/core/http/client.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

namespace {

// Timeouts applied to each request. WinHTTP has no overall request timeout, so these
// apply to each phase of the request (and, for send/receive, to each I/O operation).
constexpr int RESOLVE_TIMEOUT_MS = 10'000;
constexpr int CONNECT_TIMEOUT_MS = 10'000;
constexpr int SEND_TIMEOUT_MS = 30'000;
constexpr int RECEIVE_TIMEOUT_MS = 30'000;

// Each chunk of the request body is written as `<hex size>\r\n<data>\r\n`. We read
// body data into the middle of a single buffer, leaving enough room before it for the
// chunk header and enough after for the trailing CRLF, so that each chunk can be sent
// with a single call and without copying.
constexpr size_t CHUNK_DATA_CAPACITY = 16UL * 1024UL;
constexpr size_t CHUNK_HEADER_CAPACITY = 10;  // Up to 8 hex digits, plus CRLF
constexpr size_t CHUNK_TRAILER_SIZE = 2;      // CRLF
constexpr size_t CHUNK_BUFFER_SIZE =
    CHUNK_HEADER_CAPACITY + CHUNK_DATA_CAPACITY + CHUNK_TRAILER_SIZE;
constexpr std::string_view FINAL_CHUNK = "0\r\n\r\n";

// Response bodies are ignored, but we read (and discard) a small amount of data so that
// the connection can be reused for the next request.
constexpr DWORD MAX_RESPONSE_DRAIN_BYTES = 64UL * 1024UL;

struct WinHttpHandleDeleter {
  void operator()(void* handle) const {
    if (handle != nullptr) {
      WinHttpCloseHandle(handle);
    }
  }
};

// Owning wrapper for an HINTERNET (a void*) that closes it when destroyed
using WinHttpHandle = std::unique_ptr<void, WinHttpHandleDeleter>;

// Details gathered from WinHTTP's status callback while a request is in flight
struct RequestState {
  DWORD secure_failure_flags{0};
};

// Called by WinHTTP (on the thread making the request, since we use synchronous mode)
// to report that a TLS error has occurred, which gives us more detail than the generic
// ERROR_WINHTTP_SECURE_FAILURE that the failing API call will return.
void CALLBACK StatusCallback(
    HINTERNET /*handle*/,
    DWORD_PTR context,
    DWORD status,
    LPVOID info,
    DWORD info_length
) {
  if (status != WINHTTP_CALLBACK_STATUS_SECURE_FAILURE || context == 0 ||
      info == nullptr || info_length < sizeof(DWORD)) {
    return;
  }
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
  auto* state = reinterpret_cast<RequestState*>(context);
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
  state->secure_failure_flags |= *static_cast<const DWORD*>(info);
}

std::string DescribeSecureFailure(DWORD flags) {
  struct FlagName {
    DWORD flag;
    const char* name;
  };
  static const std::array<FlagName, 8> flag_names{{
      {WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED, "CERT_REV_FAILED"},
      {WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT, "INVALID_CERT"},
      {WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED, "CERT_REVOKED"},
      {WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA, "INVALID_CA"},
      {WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID, "CERT_CN_INVALID"},
      {WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID, "CERT_DATE_INVALID"},
      {WINHTTP_CALLBACK_STATUS_FLAG_CERT_WRONG_USAGE, "CERT_WRONG_USAGE"},
      {WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR, "SECURITY_CHANNEL_ERROR"},
  }};
  std::string result;
  for (const FlagName& entry : flag_names) {
    if ((flags & entry.flag) != 0) {
      if (!result.empty()) {
        result += ", ";
      }
      result += entry.name;
    }
  }
  return result;
}

// Converts a UTF-8 string to the UTF-16 representation that WinHTTP requires. Returns
// false if the input isn't valid UTF-8 or is too large to convert.
bool Utf8ToWide(std::string_view utf8, std::wstring& out) {
  out.clear();
  if (utf8.empty()) {
    return true;
  }
  if (utf8.size() > static_cast<size_t>(INT_MAX)) {
    return false;
  }
  const int in_size = static_cast<int>(utf8.size());
  const int out_size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), in_size, nullptr, 0
  );
  if (out_size <= 0) {
    return false;
  }
  out.resize(static_cast<size_t>(out_size));
  return MultiByteToWideChar(
             CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), in_size, out.data(), out_size
         ) == out_size;
}

// Describes a Windows error code in English, falling back to the number alone
std::string DescribeErrorCode(DWORD code) {
  std::array<char, 256> text{};
  const DWORD length = FormatMessageA(
      FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      GetModuleHandleW(L"winhttp.dll"),
      code,
      MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
      text.data(),
      static_cast<DWORD>(text.size()),
      nullptr
  );
  std::string result = std::to_string(code);
  if (length > 0) {
    std::string_view message{text.data(), length};
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' ||
                                message.back() == ' ')) {
      message.remove_suffix(1);
    }
    if (!message.empty()) {
      result += " (";
      result += message;
      result += ')';
    }
  }
  return result;
}

// Errors that indicate a problem with the request itself or with our use of the API,
// which will not be resolved by retrying later. Any other failure that occurs while
// talking to the network is assumed to be transient.
bool IsNonRetryableError(DWORD code) {
  switch (code) {
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
    case ERROR_WINHTTP_INCORRECT_HANDLE_TYPE:
    case ERROR_WINHTTP_INCORRECT_HANDLE_STATE:
    case ERROR_WINHTTP_INVALID_OPTION:
    case ERROR_WINHTTP_OPTION_NOT_SETTABLE:
    case ERROR_WINHTTP_NOT_INITIALIZED:
    case ERROR_WINHTTP_OPERATION_CANCELLED:
    case ERROR_WINHTTP_SHUTDOWN:
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_HANDLE:
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_NO_UNICODE_TRANSLATION:
      return true;
    default:
      return false;
  }
}

// Builds the result for a failed WinHTTP call. `network_phase` indicates that the
// failure occurred while communicating with the server (as opposed to while preparing
// the request), in which case it's treated as retryable unless the error code says
// otherwise.
HttpResult MakeFailure(
    const char* function,
    DWORD code,
    bool network_phase,
    const RequestState* state = nullptr
) {
  const bool retryable = network_phase && !IsNonRetryableError(code);
  HttpResult result{
      retryable ? HttpResultType::GotNoResponse_Retryable
                : HttpResultType::GotNoResponse_NonRetryable,
      0
  };
  result.error_code = static_cast<int>(code);
  result.error_message = std::string(function) + ": error " + DescribeErrorCode(code);
  if (state != nullptr && state->secure_failure_flags != 0) {
    result.error_message +=
        "; TLS failure: " + DescribeSecureFailure(state->secure_failure_flags);
  }
  return result;
}

class WinHttpClient final : public IHttpClient {
 private:
  // Shared with the subsystem, so that the session remains valid for as long as any
  // client that uses it exists
  std::shared_ptr<void> _session;

  // Scratch space for framing each chunk of a request body (and, afterward, for reading
  // the response). A client never has more than one request in flight, so all of its
  // requests can share one buffer, which is allocated once rather than per request.
  std::vector<char> _buffer;

 public:
  explicit WinHttpClient(std::shared_ptr<void> session)
      : _session(std::move(session)), _buffer(CHUNK_BUFFER_SIZE) {
    DATADOG_ASSERT(_session, "WinHttpClient constructed with null session handle");
  }

  ~WinHttpClient() override = default;

  // An IHttpClient is never copied or moved
  WinHttpClient(const WinHttpClient&) = delete;
  WinHttpClient& operator=(const WinHttpClient&) = delete;
  WinHttpClient(WinHttpClient&&) = delete;
  WinHttpClient& operator=(WinHttpClient&&) = delete;

  HttpResult Post(
      const char* url, const char* headers, HttpBodyWriter body_writer
  ) override {
    // Convert the URL to UTF-16 and split it into the pieces that WinHTTP wants
    std::wstring wide_url;
    if (!Utf8ToWide(url != nullptr ? url : "", wide_url)) {
      return MakeFailure("Utf8ToWide(url)", ERROR_NO_UNICODE_TRANSLATION, false);
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    // Nonzero lengths with null pointers ask WinHttpCrackUrl to point into wide_url
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(
            wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &parts
        )) {
      return MakeFailure("WinHttpCrackUrl", GetLastError(), false);
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTP &&
        parts.nScheme != INTERNET_SCHEME_HTTPS) {
      return MakeFailure("WinHttpCrackUrl", ERROR_WINHTTP_UNRECOGNIZED_SCHEME, false);
    }
    if (parts.dwHostNameLength == 0) {
      return MakeFailure("WinHttpCrackUrl", ERROR_WINHTTP_INVALID_URL, false);
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring object;
    if (parts.dwUrlPathLength > 0) {
      object.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    }
    if (parts.dwExtraInfoLength > 0) {
      object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (object.empty()) {
      object = L"/";
    }

    // Convert the request headers, which must be CRLF-delimited with a trailing CRLF,
    // and tell the server that we're going to send a chunked body
    const std::string_view headers_view = headers != nullptr ? headers : "";
    DATADOG_ASSERT(
        headers_view.empty() ||
            (headers_view.size() >= 2 &&
             headers_view.substr(headers_view.size() - 2) == "\r\n"),
        "HTTP headers must be CRLF-delimited with a trailing CRLF"
    );
    std::wstring wide_headers;
    if (!Utf8ToWide(headers_view, wide_headers)) {
      return MakeFailure("Utf8ToWide(headers)", ERROR_NO_UNICODE_TRANSLATION, false);
    }
    wide_headers += L"Transfer-Encoding: chunked\r\n";

    // Create the connection and request handles: neither performs any network I/O
    const WinHttpHandle connection(
        WinHttpConnect(_session.get(), host.c_str(), parts.nPort, 0)
    );
    if (!connection) {
      return MakeFailure("WinHttpConnect", GetLastError(), false);
    }
    const DWORD request_flags =
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    const WinHttpHandle request(WinHttpOpenRequest(
        connection.get(),
        L"POST",
        object.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        request_flags
    ));
    if (!request) {
      return MakeFailure("WinHttpOpenRequest", GetLastError(), false);
    }

    // Don't follow redirects, so that any 3xx response is reported to the caller as
    // it would be by the libcurl client. Both of these calls are best-effort.
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(
        request.get(),
        WINHTTP_OPTION_REDIRECT_POLICY,
        &redirect_policy,
        sizeof(redirect_policy)
    );
    WinHttpSetTimeouts(
        request.get(),
        RESOLVE_TIMEOUT_MS,
        CONNECT_TIMEOUT_MS,
        SEND_TIMEOUT_MS,
        RECEIVE_TIMEOUT_MS
    );

    // Subscribe to TLS failure details, which will be recorded in `state`
    RequestState state;
    WinHttpSetStatusCallback(
        request.get(), StatusCallback, WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0
    );

    if (!WinHttpAddRequestHeaders(
            request.get(),
            wide_headers.c_str(),
            static_cast<DWORD>(wide_headers.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE
        )) {
      return MakeFailure("WinHttpAddRequestHeaders", GetLastError(), false);
    }

    // Send the request headers. The body's length isn't known up front: with a
    // 'Transfer-Encoding: chunked' header in place, WinHTTP lets us write the body in
    // pieces afterward, but we're responsible for framing each of them as a chunk.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    const auto context = reinterpret_cast<DWORD_PTR>(&state);
    if (!WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            context
        )) {
      return MakeFailure("WinHttpSendRequest", GetLastError(), true, &state);
    }

    // Stream the body: read each chunk of data from the writer, frame it, and send it
    char* const data = _buffer.data() + CHUNK_HEADER_CAPACITY;
    for (;;) {
      const size_t data_size = body_writer(data, CHUNK_DATA_CAPACITY);
      if (data_size == HTTP_WRITE_RESULT_ABORT) {
        return MakeFailure("HttpBodyWriter", ERROR_OPERATION_ABORTED, false);
      }
      if (data_size == HTTP_WRITE_RESULT_EOF) {
        break;
      }
      DATADOG_ASSERT(data_size <= CHUNK_DATA_CAPACITY, "HttpBodyWriter overran buffer");
      if (data_size > CHUNK_DATA_CAPACITY) {
        return MakeFailure("HttpBodyWriter", ERROR_BUFFER_OVERFLOW, false);
      }

      // Write the chunk size as hex immediately before the data, preceded by CRLF
      char* frame_start = data;
      *--frame_start = '\n';
      *--frame_start = '\r';
      for (size_t remaining = data_size; remaining > 0; remaining >>= 4U) {
        *--frame_start = "0123456789abcdef"[remaining & 0xFU];
      }
      // And follow the data with a CRLF
      data[data_size] = '\r';
      data[data_size + 1] = '\n';

      const auto frame_size =
          static_cast<size_t>(data + data_size + CHUNK_TRAILER_SIZE - frame_start);
      if (!WriteAll(request.get(), frame_start, frame_size)) {
        return MakeFailure("WinHttpWriteData", GetLastError(), true, &state);
      }
    }
    if (!WriteAll(request.get(), FINAL_CHUNK.data(), FINAL_CHUNK.size())) {
      return MakeFailure("WinHttpWriteData", GetLastError(), true, &state);
    }

    // Wait for the response and get its status code
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
      return MakeFailure("WinHttpReceiveResponse", GetLastError(), true, &state);
    }
    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_code_size,
            WINHTTP_NO_HEADER_INDEX
        )) {
      return MakeFailure("WinHttpQueryHeaders", GetLastError(), true, &state);
    }

    // We don't use the response body, but consume it so the connection can be reused
    DrainResponse(request.get(), _buffer);

    return HttpResult{HttpResultType::GotResponse, static_cast<int>(status_code)};
  }

 private:
  // Writes all of the given data to the request body, returning false on failure
  static bool WriteAll(HINTERNET request, const char* data, size_t size) {
    while (size > 0) {
      DWORD num_written = 0;
      if (!WinHttpWriteData(request, data, static_cast<DWORD>(size), &num_written)) {
        return false;
      }
      if (num_written == 0) {
        SetLastError(ERROR_WINHTTP_CONNECTION_ERROR);
        return false;
      }
      data += num_written;
      size -= num_written;
    }
    return true;
  }

  // Reads and discards up to MAX_RESPONSE_DRAIN_BYTES of the response body, using the
  // given buffer as scratch space. Errors are ignored: the response has already been
  // received, so they can't change the outcome of the request.
  static void DrainResponse(HINTERNET request, std::vector<char>& scratch) {
    DWORD total_read = 0;
    while (total_read < MAX_RESPONSE_DRAIN_BYTES) {
      DWORD num_available = 0;
      if (!WinHttpQueryDataAvailable(request, &num_available) || num_available == 0) {
        return;
      }
      DWORD num_read = 0;
      const auto to_read =
          static_cast<DWORD>(std::min<size_t>(num_available, scratch.size()));
      if (!WinHttpReadData(request, scratch.data(), to_read, &num_read) ||
          num_read == 0) {
        return;
      }
      total_read += num_read;
    }
  }
};

class WinHttpSubsystem final : public IHttpSubsystem {
 private:
  std::shared_ptr<void> _session;

 public:
  explicit WinHttpSubsystem(std::shared_ptr<void> session)
      : _session(std::move(session)) {}

  ~WinHttpSubsystem() override = default;

  // The IHttpSubsystem is never copied or moved
  WinHttpSubsystem(const WinHttpSubsystem&) = delete;
  WinHttpSubsystem& operator=(const WinHttpSubsystem&) = delete;
  WinHttpSubsystem(WinHttpSubsystem&&) = delete;
  WinHttpSubsystem& operator=(WinHttpSubsystem&&) = delete;

  std::string_view GetName() const override { return "winhttp"; }

  // WinHTTP is part of the operating system, whose version is reported separately
  std::string_view GetVersion() const override { return ""; }

  std::unique_ptr<IHttpClient> CreateClient() override {
    return std::make_unique<WinHttpClient>(_session);
  }
};

}  // namespace

std::unique_ptr<IHttpSubsystem> Http::Init(const DiagnosticLogger& logger) {
  // Prefer the system's automatic proxy configuration (Windows 8.1 and later), falling
  // back to the default proxy settings if the OS doesn't support it
  HINTERNET session = WinHttpOpen(
      L"dd-sdk-cpp",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0
  );
  if (session == nullptr) {
    session = WinHttpOpen(
        L"dd-sdk-cpp",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
  }
  if (session == nullptr) {
    logger.Error(
        "Failed to initialize WinHTTP",
        {{"error_code", static_cast<int64_t>(GetLastError())}}
    );
    return nullptr;
  }

  // The session is closed when the subsystem and all of its clients are destroyed
  std::shared_ptr<void> shared_session(session, WinHttpHandleDeleter{});
  return std::make_unique<WinHttpSubsystem>(std::move(shared_session));
}

}  // namespace datadog::impl
