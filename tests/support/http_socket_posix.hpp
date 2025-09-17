// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

/**
 * POSIX implementation of the cross-platform TCP socket wrapper used by
 * MockHttpServer.
 */
struct Socket {
  int fd;

  Socket() : fd(-1) {}

  bool Create() {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    return fd != -1;
  }

  bool SetReuseAddr() {
    if (fd == -1) {
      return false;
    }
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0;
  }

  bool Bind(uint16_t port) {
    if (fd == -1) {
      return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    return bind(fd, (sockaddr*)&server_addr, sizeof(server_addr)) == 0;
  }

  uint16_t GetBoundPort() const {
    if (fd == -1) {
      return 0;
    }

    sockaddr_in server_addr{};
    socklen_t addr_len = sizeof(server_addr);
    if (getsockname(fd, (sockaddr*)&server_addr, &addr_len) == 0) {
      return ntohs(server_addr.sin_port);
    }
    return 0;
  }

  bool Listen(int backlog = 5) {
    if (fd == -1) {
      return false;
    }
    return listen(fd, backlog) == 0;
  }

  Socket Accept(int timeout_ms) {
    Socket conn;
    if (fd == -1) {
      return conn;
    }

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int poll_result = poll(&pfd, 1, timeout_ms);
    if (poll_result <= 0) {
      // Timeout or error
      return conn;
    }

    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    conn.fd = accept(fd, (sockaddr*)&client_addr, &client_len);
    return conn;
  }

  int Recv(char* buffer, size_t buffer_size, int timeout_ms = 20) {
    if (fd == -1) {
      return -1;
    }

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;

    int poll_result = poll(&pfd, 1, timeout_ms);
    if (poll_result <= 0) {
      return poll_result == 0 ? 0 : -1;  // 0 for timeout, -1 for error
    }

    return recv(fd, buffer, buffer_size, 0);
  }

  int Send(const char* data, size_t length) {
    if (fd == -1) {
      return -1;
    }
    return send(fd, data, length, 0);
  }

  void Close() {
    if (fd != -1) {
      close(fd);
      fd = -1;
    }
  }

  bool IsValid() const { return fd != -1; }
};
