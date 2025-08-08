#pragma once

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#error "Windows support NYI"
#else
#include <cerrno>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>

// Wrap socket.h/WinSock for cross-platform support
#ifdef _WIN32
struct Socket
{
#error "Windows support NYI"
};
#else
struct Socket
{
    int fd;

    Socket()
        : fd(-1)
    {}

    bool Create()
    {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        return fd != -1;
    }

    bool SetReuseAddr()
    {
        if (fd == -1)
        {
            return false;
        }
        int opt = 1;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0;
    }

    bool Bind(uint16_t port)
    {
        if (fd == -1)
        {
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        return bind(fd, (sockaddr*)&server_addr, sizeof(server_addr)) == 0;
    }

    uint16_t GetBoundPort() const
    {
        if (fd == -1)
        {
            return 0;
        }

        sockaddr_in server_addr{};
        socklen_t addr_len = sizeof(server_addr);
        if (getsockname(fd, (sockaddr*)&server_addr, &addr_len) == 0)
        {
            return ntohs(server_addr.sin_port);
        }
        return 0;
    }

    bool Listen(int backlog = 5)
    {
        if (fd == -1)
        {
            return false;
        }
        return listen(fd, backlog) == 0;
    }

    Socket Accept(int timeout_ms)
    {
        Socket conn;
        if (fd == -1)
        {
            return conn;
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int poll_result = poll(&pfd, 1, timeout_ms);
        if (poll_result <= 0)
        {
            // Timeout or error
            return conn;
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        conn.fd = accept(fd, (sockaddr*)&client_addr, &client_len);
        return conn;
    }

    int Recv(char* buffer, size_t buffer_size, int timeout_ms = 20)
    {
        if (fd == -1)
        {
            return -1;
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        int poll_result = poll(&pfd, 1, timeout_ms);
        if (poll_result <= 0)
        {
            return poll_result == 0 ? 0 : -1; // 0 for timeout, -1 for error
        }

        return recv(fd, buffer, buffer_size, 0);
    }

    int Send(const char* data, size_t length)
    {
        if (fd == -1)
        {
            return -1;
        }
        return send(fd, data, length, 0);
    }

    void Close()
    {
        if (fd != -1)
        {
            close(fd);
            fd = -1;
        }
    }

    bool IsValid() const
    {
        return fd != -1;
    }
};
#endif

// Shut down the server gracefully if tests are aborted
static std::atomic<bool> g_shutdown_requested{ false };

static void signal_handler(int signum)
{
    (void)signum;
    g_shutdown_requested = true;
}

struct MockHttpServer
{
    uint16_t port;        // TCP port the server is bound to
    std::string response; // Desired HTTP response to use as reply for all requests
    std::vector<std::string> requests; // Record of all HTTP requests received

    explicit MockHttpServer(uint16_t in_port = 0)
        : port(in_port)
        , _running(false)
    {
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        if (!_sock.Create())
        {
            return;
        }

        _sock.SetReuseAddr();

        if (!_sock.Bind(port))
        {
            _sock.Close();
            return;
        }

        if (port == 0)
        {
            port = _sock.GetBoundPort();
        }

        SetResponseStatus(200);
    }

    ~MockHttpServer()
    {
        Stop();
        _sock.Close();
    }

    void Start()
    {
        if (!_sock.IsValid() || _running.load())
        {
            return;
        }

        if (!_sock.Listen())
        {
            return;
        }

        _running = true;
        _server_thread = std::thread(&MockHttpServer::ServerLoop, this);
    }

    void Stop()
    {
        _running = false;
        g_shutdown_requested = true;
        if (_server_thread.joinable())
        {
            _server_thread.join();
        }
    }

    void SetResponseStatus(int status_code)
    {
        std::string_view response_body = "mock-response";
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code << "\r\n";
        oss << "Content-Length: " << response_body.length() << "\r\n";
        oss << "Connection: close\r\n";
        oss << "\r\n";
        oss << response_body;
        response = oss.str();
    }

    std::string BuildURL(std::string_view path) const
    {
        std::ostringstream oss;
        oss << "http://127.0.0.1:" << port << path;
        return oss.str();
    }

    uint16_t GetPort() const
    {
        return port;
    }

private:
    void ServerLoop()
    {
        while (_running.load() && !g_shutdown_requested.load())
        {
            const int timeout_ms = 50;
            Socket conn = _sock.Accept(timeout_ms);
            if (!conn.IsValid())
            {
                continue;
            }
            HandleClient(conn);
        }
    }

    void HandleClient(Socket conn)
    {
        // Accumulate the text of the HTTP request into a string
        std::string request;
        char buffer[1024];
        while (true)
        {
            // Read from the socket, exiting our loop if no more data (or timeout)
            const int num_bytes_read = conn.Recv(buffer, sizeof(buffer) - 1);
            if (num_bytes_read <= 0)
            {
                break;
            }
            buffer[num_bytes_read] = '\0';
            request += buffer;
        }
        requests.push_back(request);

        // We don't implement any HTTP-request-handling logic; we just record the
        // request for tests to examine, and we respond with whatever response the test
        // instructed us to send
        conn.Send(response.data(), response.size());
        conn.Close();
    }

    Socket _sock;
    std::atomic<bool> _running;
    std::thread _server_thread;
};
