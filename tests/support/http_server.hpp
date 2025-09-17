// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
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

  // If set by test, server will close the client connection after reading the request,
  // without sending a response
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

  void HandleClient(Socket conn) {
    // Accumulate the text of the HTTP request into a string
    std::string request;
    char buffer[1024];
    while (true) {
      // Read from the socket, exiting our loop if no more data (or timeout)
      const int num_bytes_read = conn.Recv(buffer, sizeof(buffer) - 1);
      if (num_bytes_read <= 0) {
        break;
      }
      buffer[num_bytes_read] = '\0';
      request += buffer;
    }
    requests.push_back(request);

    // If configured, simulate a server that drops the connection
    if (close_after_read) {
      conn.Close();
      return;
    }

    // We don't implement any HTTP-request-handling logic; we just record the request
    // for tests to examine, and we respond with whatever response the test instructed
    // us to send
    conn.Send(response.data(), response.size());
    conn.Close();
  }

  Socket _sock;
  std::atomic<bool> _running{false};
  std::thread _server_thread;
};
