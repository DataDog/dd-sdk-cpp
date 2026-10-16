// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Wrap socket.h/WinSock for cross-platform support
#ifdef _WIN32
#include "http_socket_windows.hpp"
#else
#include "http_socket_posix.hpp"
#endif

/**
 * Working, bare-bones HTTP server used for testing real HTTP client functionality.
 *
 * Wraps a TCP socket that's bound on ctor
 */
struct MockHttpServer {
  // Port on which the server is accepting connections
  uint16_t port;

  // Raw text that we'll send in reply to any and all connections; tests can override or
  // call SetResponseStatus() to populate with a valid-enough HTTP response
  std::string response;

  // If set by test, server will close the client connection after reading the complete
  // request, without sending a response
  bool close_after_read{false};

  // All HTTP requests received will be recorded here for tests to examine
  std::vector<std::string> requests;

  explicit MockHttpServer(uint16_t in_port = 0) : port(in_port) {
    // Use platform-agnostic wrapper interface for socket operations
    if (!_sock.Create()) {
      return;
    }

    // Set REUSEADDR to avoid port conflicts
    _sock.SetReuseAddr();

    // Bind to the configured port
    if (!_sock.Bind(port)) {
      _sock.Close();
      return;
    }

    // If configured to auto-bind, cache the actual port in use
    if (port == 0) {
      port = _sock.GetBoundPort();
    }

    // Default to replying with a valid HTTP/1.1 200 response; tests can override
    SetResponseStatus(200);
  }

  ~MockHttpServer() {
    // Ensure that the server is stopped at end of tests
    Stop();
    _sock.Close();
  }

  void Start() {
    // Abort if shut down or socket init failed
    if (!_sock.IsValid() || _running.load()) {
      return;
    }

    // Prepare to accept incoming requests; abort if socket not usable
    if (!_sock.Listen()) {
      return;
    }

    // Start a background thread to run the accept-connections-and-reply loop.
    // NOTE: Thread writes to requests vector without synchronization, so to avoid the
    // possibility of a data race, tests should call Stop() before reading from the
    // requests vector
    _running = true;
    _server_thread = std::thread(&MockHttpServer::ServerLoop, this);
  }

  void Stop() {
    // Signal shutdown and wait for thread to exit
    _running = false;
    if (_server_thread.joinable()) {
      _server_thread.join();
    }
  }

  /**
   * Configures the server to respond to all requests with the given HTTP status.
   */
  void SetResponseStatus(int status_code) {
    std::string_view response_body = "mock-response";
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << "\r\n";
    oss << "Content-Length: " << response_body.length() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << response_body;
    response = oss.str();
  }

  /**
   * Constructs a fully-qualified URL that will resolve to this server.
   */
  std::string BuildURL(std::string_view path) const {
    std::ostringstream oss;
    oss << "http://127.0.0.1:" << port << path;
    return oss.str();
  }

  uint16_t GetPort() const { return port; }

 private:
  void ServerLoop() {
    // Loop indefinitely, actively checking for shutdown quite frequently
    const int timeout_ms = 50;
    while (_running.load()) {
      // If we get a client connection, handle it
      Socket conn = _sock.Accept(timeout_ms);
      if (conn.IsValid()) {
        HandleClient(conn);
      }
    }
  }

  // The longest we'll wait for a client to send more of its request, and the longest
  // we'll spend receiving a single request, before giving up on the client
  static constexpr int RECV_TIMEOUT_MS = 2000;
  static constexpr std::chrono::seconds REQUEST_TIMEOUT{10};

  // Returns a lowercase copy of the given ASCII string
  static std::string ToLower(std::string_view s) {
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return result;
  }

  // Returns true if `body` holds a complete chunked-encoded body, i.e. all of its
  // chunks, the final zero-length chunk, and the blank line that ends the body
  static bool IsChunkedBodyComplete(std::string_view body) {
    size_t pos = 0;
    while (true) {
      // Each chunk begins with its size in hex, on a line by itself
      const size_t line_end = body.find("\r\n", pos);
      if (line_end == std::string_view::npos) {
        return false;
      }
      const std::string size_line(body.substr(pos, line_end - pos));
      char* size_end = nullptr;
      const size_t chunk_size = std::strtoull(size_line.c_str(), &size_end, 16);
      if (size_end == size_line.c_str()) {
        return true;  // Malformed; stop waiting and let the test examine what we have
      }
      pos = line_end + 2;

      // The zero-length chunk ends the body (we don't expect any trailers)
      if (chunk_size == 0) {
        return body.size() >= pos + 2;
      }

      // Otherwise, the chunk's data and a trailing CRLF must follow
      if (body.size() < pos + chunk_size + 2) {
        return false;
      }
      pos += chunk_size + 2;
    }
  }

  // Returns true if `request` holds a complete HTTP request: all of the headers, along
  // with a body of whatever length the headers say to expect
  static bool IsRequestComplete(const std::string& request) {
    const size_t headers_end = request.find("\r\n\r\n");
    if (headers_end == std::string::npos) {
      return false;
    }
    const std::string headers =
        ToLower(std::string_view(request).substr(0, headers_end));
    const std::string_view body = std::string_view(request).substr(headers_end + 4);

    // Header names are matched at the start of a line, which is preceded by CRLF
    if (headers.find("\r\ntransfer-encoding: chunked") != std::string::npos) {
      return IsChunkedBodyComplete(body);
    }
    const std::string content_length_header = "\r\ncontent-length:";
    const size_t content_length_pos = headers.find(content_length_header);
    if (content_length_pos != std::string::npos) {
      const char* value =
          headers.c_str() + content_length_pos + content_length_header.size();
      return body.size() >= std::strtoull(value, nullptr, 10);
    }

    // With no body framing, the request ends with its headers
    return true;
  }

  void HandleClient(Socket conn) {
    // Accumulate the text of the HTTP request into a string, until we've received all
    // of it. We can't take a pause in the incoming data to mean that the client is
    // finished, because clients may legitimately pause, e.g. between sending headers
    // and sending a body. If we replied and closed the connection too early, a client
    // that was still sending could see its request fail.
    std::string request;
    char buffer[1024];
    bool headers_received = false;
    const auto deadline = std::chrono::steady_clock::now() + REQUEST_TIMEOUT;
    while (!IsRequestComplete(request) && std::chrono::steady_clock::now() < deadline) {
      // Read from the socket, exiting our loop if the client closes the connection or
      // stops sending data (or on error)
      const int num_bytes_read = conn.Recv(buffer, sizeof(buffer), RECV_TIMEOUT_MS);
      if (num_bytes_read <= 0) {
        break;
      }
      request.append(buffer, static_cast<size_t>(num_bytes_read));

      // As soon as we've got the headers, tell clients that asked us to (as libcurl
      // does for some requests) that they may proceed to send the body. Otherwise,
      // they'd wait for our permission, and we'd wait for the body.
      const size_t headers_end = request.find("\r\n\r\n");
      if (!headers_received && headers_end != std::string::npos) {
        headers_received = true;
        const std::string headers =
            ToLower(std::string_view(request).substr(0, headers_end));
        if (headers.find("\r\nexpect: 100-continue") != std::string::npos) {
          static const std::string_view continue_response =
              "HTTP/1.1 100 Continue\r\n\r\n";
          conn.Send(continue_response.data(), continue_response.size());
        }
      }
    }
    requests.push_back(request);

    // If configured, simulate a server that drops the connection
    if (close_after_read) {
      conn.Close();
      return;
    }

    // Beyond recognizing where a request ends, we don't implement any HTTP-handling
    // logic; we just record the request for tests to examine, and we respond with
    // whatever response the test instructed us to send
    conn.Send(response.data(), response.size());
    conn.Close();
  }

  Socket _sock;
  std::atomic<bool> _running{false};
  std::thread _server_thread;
};
