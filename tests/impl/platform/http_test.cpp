#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>

#include "mock/http_server.hpp"
#include "platform/http.hpp"

using namespace datadog;

// Tag [platform-http] describes tests used to validate that a platform-specific or
// user-provided implementation of platform/http.hpp behaves as expected

TEST_CASE("Http", "[unit][platform-http]")
{
    SECTION("M create a valid IHttpSubsystem W static Init is called")
    {
        auto http = platform::Http::Init();
        REQUIRE(http != nullptr);
    }
}

TEST_CASE("IHttpSubsystem", "[unit][platform-http]")
{
    // Given an HTTP subsystem
    auto http = platform::Http::Init();
    REQUIRE(http != nullptr);

    SECTION("M create a valid IHttpClient W ")
    {
        auto client = http->CreateClient();
        REQUIRE(client != nullptr);
    }
}

TEST_CASE("IHttpClient", "[unit][platform-http]")
{
    // Given an HTTP client
    auto http = platform::Http::Init();
    REQUIRE(http != nullptr);
    auto client = http->CreateClient();
    REQUIRE(client != nullptr);

    SECTION("M get valid response W request is initiated")
    {
        // Given an HTTP server
        MockHttpServer server(0);
        server.Start();

        // And a simple request payload
        const std::string test_body = "mock request";
        size_t bytes_sent = 0;
        auto body_writer = [&](char* buffer, size_t num_bytes) -> size_t
        {
            if (bytes_sent >= test_body.size())
            {
                return platform::HTTP_WRITE_RESULT_EOF;
            }
            size_t to_copy = std::min(num_bytes, test_body.size() - bytes_sent);
            std::memcpy(buffer, test_body.data() + bytes_sent, to_copy);
            bytes_sent += to_copy;
            return to_copy;
        };

        // When the client sends that payload
        const std::string url = server.BuildURL("/api/v1/test?foo=hello&bar=42");
        const std::string headers = "Content-Type: text/plain\n";
        auto result = client->Post(url, headers, body_writer);

        // Then it gets a valid response
        REQUIRE(result.type == platform::HttpResultType::GotResponse);
        REQUIRE(result.status_code == 200);

        // And the server receives the expected request
        server.Stop();
        REQUIRE(server.requests.size() == 1);
        const auto req = server.requests.front();
        auto req_pos = req.find("POST /api/v1/test?foo=hello&bar=42 HTTP/1.1\r\n");
        REQUIRE(req_pos == 0);
        auto header_pos = req.find("Content-Type: text/plain\r\n");
        REQUIRE(header_pos > req_pos);
        auto delim_pos = req.find("\r\n\r\n");
        REQUIRE(delim_pos > header_pos);
        auto body_text_pos = req.find("mock request");
        REQUIRE(body_text_pos > delim_pos);
    }
}
