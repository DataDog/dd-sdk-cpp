// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#include <cstdint>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

/**
 * Windows implementation of the cross-platform TCP socket wrapper used by
 * MockHttpServer.
 */
struct Socket {
  SOCKET sock;
  static bool winsock_initialized;

  Socket() : sock(INVALID_SOCKET) {
    if (!winsock_initialized) {
      WSADATA wsaData;
      WSAStartup(MAKEWORD(2, 2), &wsaData);
      winsock_initialized = true;
    }
  }

  bool Create() {
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    return sock != INVALID_SOCKET;
  }

  bool SetReuseAddr() {
    if (sock == INVALID_SOCKET) {
      return false;
    }
    int opt = 1;
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) ==
           0;
  }

  bool Bind(uint16_t port) {
    if (sock == INVALID_SOCKET) {
      return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    return bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == 0;
  }

  uint16_t GetBoundPort() const {
    if (sock == INVALID_SOCKET) {
      return 0;
    }

    sockaddr_in server_addr{};
    int addr_len = sizeof(server_addr);
    if (getsockname(sock, (sockaddr*)&server_addr, &addr_len) == 0) {
      return ntohs(server_addr.sin_port);
    }
    return 0;
  }

  bool Listen(int backlog = 5) {
    if (sock == INVALID_SOCKET) {
      return false;
    }
    return listen(sock, backlog) == 0;
  }

  Socket Accept(int timeout_ms) {
    Socket conn;
    if (sock == INVALID_SOCKET) {
      return conn;
    }

    WSAPOLLFD pfd{};
    pfd.fd = sock;
    pfd.events = POLLIN;
    int poll_result = WSAPoll(&pfd, 1, timeout_ms);
    if (poll_result <= 0) {
      // Timeout or error
      return conn;
    }

    sockaddr_in client_addr{};
    int client_len = sizeof(client_addr);

    conn.sock = accept(sock, (sockaddr*)&client_addr, &client_len);
    return conn;
  }

  int Recv(char* buffer, size_t buffer_size, int timeout_ms = 20) {
    if (sock == INVALID_SOCKET) {
      return -1;
    }

    WSAPOLLFD pfd{};
    pfd.fd = sock;
    pfd.events = POLLIN;

    int poll_result = WSAPoll(&pfd, 1, timeout_ms);
    if (poll_result <= 0) {
      return poll_result == 0 ? 0 : -1;  // 0 for timeout, -1 for error
    }

    return recv(sock, buffer, static_cast<int>(buffer_size), 0);
  }

  int Send(const char* data, size_t length) {
    if (sock == INVALID_SOCKET) {
      return -1;
    }
    return send(sock, data, static_cast<int>(length), 0);
  }

  void Close() {
    if (sock != INVALID_SOCKET) {
      closesocket(sock);
      sock = INVALID_SOCKET;
    }
  }

  bool IsValid() const { return sock != INVALID_SOCKET; }
};

bool Socket::winsock_initialized = false;

#endif
