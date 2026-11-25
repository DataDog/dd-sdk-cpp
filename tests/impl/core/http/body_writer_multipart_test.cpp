// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/http/body_writer_multipart.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "support/catch.hpp"

using namespace datadog::impl;

/**
 * Reads the whole body from `writer`, `chunk_size` bytes at a time, checking that each
 * call fills the buffer completely unless the body has ended.
 */
static std::string read_body(HttpBodyWriter& writer, size_t chunk_size) {
  std::string body;
  std::vector<char> buffer(chunk_size);
  while (true) {
    const size_t num_bytes_written = writer(buffer.data(), buffer.size());
    REQUIRE(num_bytes_written <= chunk_size);
    if (num_bytes_written == HTTP_WRITE_RESULT_EOF) {
      return body;
    }
    body.append(buffer.data(), num_bytes_written);
  }
}

// A part with a binary body, including a zero byte and bytes above 0x7F
static const std::string BINARY_DATA("\x01\x00\xFF\x80 end", 8);

static std::vector<MultipartPart> make_sample_parts() {
  return {
      {"event", "event.json", "application/json", R"({"a":1})"},
      {"profile.pprof", "profile.pprof", "application/octet-stream", BINARY_DATA},
      {"note", "", "text/plain", "hi"},
  };
}

/** Returns a writer of a body made of `parts`, separated by `boundary`. */
static MultipartBodyWriter make_writer(
    std::string_view boundary, const std::vector<MultipartPart>& parts
) {
  return MultipartBodyWriter(boundary, parts.data(), parts.size());
}

static std::string expected_sample_body() {
  std::string body;
  body += "--B\r\n";
  body += "Content-Disposition: form-data; name=\"event\"; filename=\"event.json\"\r\n";
  body += "Content-Type: application/json\r\n";
  body += "\r\n";
  body += R"({"a":1})";
  body += "\r\n--B\r\n";
  body +=
      "Content-Disposition: form-data; name=\"profile.pprof\"; "
      "filename=\"profile.pprof\"\r\n";
  body += "Content-Type: application/octet-stream\r\n";
  body += "\r\n";
  body += BINARY_DATA;
  body += "\r\n--B\r\n";
  body += "Content-Disposition: form-data; name=\"note\"\r\n";
  body += "Content-Type: text/plain\r\n";
  body += "\r\n";
  body += "hi";
  body += "\r\n--B--\r\n";
  return body;
}

TEST_CASE("MultipartBodyWriter", "[unit][writer]") {
  SECTION("M write the parts separated by the boundary W the buffer is large") {
    HttpBodyWriter writer = make_writer("B", make_sample_parts());

    const std::string expected = expected_sample_body();
    REQUIRE(read_body(writer, 4096) == expected);
  }

  SECTION("M write the same body W the buffer is any size") {
    // The body is the same however it is split up, including where a read ends in the
    // middle of a boundary line, a header, or a part's data
    const std::string expected = expected_sample_body();
    for (size_t chunk_size = 1; chunk_size <= expected.size() + 1; chunk_size++) {
      HttpBodyWriter writer = make_writer("B", make_sample_parts());

      INFO("chunk size " << chunk_size);
      REQUIRE(read_body(writer, chunk_size) == expected);
    }
  }

  SECTION("M write nothing more W the end of the body was reached") {
    HttpBodyWriter writer = make_writer("B", make_sample_parts());
    read_body(writer, 4096);

    char buffer[16];
    REQUIRE(writer(buffer, sizeof(buffer)) == HTTP_WRITE_RESULT_EOF);
    REQUIRE(writer(buffer, sizeof(buffer)) == HTTP_WRITE_RESULT_EOF);
  }

  SECTION("M write nothing and not advance W the buffer has no room") {
    HttpBodyWriter writer = make_writer("B", make_sample_parts());

    char buffer[16];
    REQUIRE(writer(buffer, 0) == 0);

    // Then the body is still whole
    REQUIRE(read_body(writer, 7) == expected_sample_body());
  }

  SECTION("M write an empty part W a part has no data") {
    HttpBodyWriter writer = make_writer(
        "B", {{"first", "", "text/plain", ""}, {"second", "", "text/plain", "x"}}
    );

    std::string expected;
    expected += "--B\r\n";
    expected += "Content-Disposition: form-data; name=\"first\"\r\n";
    expected += "Content-Type: text/plain\r\n";
    expected += "\r\n";
    expected += "\r\n--B\r\n";
    expected += "Content-Disposition: form-data; name=\"second\"\r\n";
    expected += "Content-Type: text/plain\r\n";
    expected += "\r\n";
    expected += "x";
    expected += "\r\n--B--\r\n";
    REQUIRE(read_body(writer, 5) == expected);
  }

  SECTION("M write only the closing boundary W there are no parts") {
    HttpBodyWriter writer = MultipartBodyWriter("B", nullptr, 0);

    REQUIRE(read_body(writer, 3) == "--B--\r\n");
  }

  SECTION("M continue from the same place W a writer is copied part way") {
    MultipartBodyWriter original = make_writer("B", make_sample_parts());
    char buffer[40];
    REQUIRE(original(buffer, sizeof(buffer)) == sizeof(buffer));

    // When the writer is copied, which a std::function does with what it holds
    HttpBodyWriter copy = original;

    // Then each carries on from where the original had got to, independently
    const std::string expected = expected_sample_body();
    const std::string rest = expected.substr(sizeof(buffer));
    HttpBodyWriter original_as_function = original;
    REQUIRE(read_body(copy, 11) == rest);
    REQUIRE(read_body(original_as_function, 13) == rest);
  }

  SECTION("M not depend on the parts' storage W the writer is made") {
    // The writer owns each part's headers: only the data it is given must stay valid,
    // not the boundary, the array of parts, or the parts' names and types
    HttpBodyWriter writer = [&] {
      std::string boundary = "temporary-boundary";
      std::string name = "temporary-name";
      std::string content_type = "temporary/type";
      const std::array<MultipartPart, 1> parts = {
          {{name, "", content_type, "payload"}}
      };
      return HttpBodyWriter(MultipartBodyWriter(boundary, parts.data(), parts.size()));
    }();

    std::string expected;
    expected += "--temporary-boundary\r\n";
    expected += "Content-Disposition: form-data; name=\"temporary-name\"\r\n";
    expected += "Content-Type: temporary/type\r\n";
    expected += "\r\n";
    expected += "payload";
    expected += "\r\n--temporary-boundary--\r\n";
    REQUIRE(read_body(writer, 9) == expected);
  }
}

TEST_CASE("MultipartBoundary", "[unit][writer]") {
  SECTION("M make a boundary with only valid characters W it is made") {
    const MultipartBoundary random = MultipartBoundary::Random();
    const std::string_view boundary = random.Boundary();

    // Not empty, and within the 70 characters that RFC 2046 allows
    REQUIRE_FALSE(boundary.empty());
    REQUIRE(boundary.size() <= 70);

    // Letters, digits and hyphens are all valid in a boundary
    const bool valid = std::all_of(boundary.begin(), boundary.end(), [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '-';
    });
    REQUIRE(valid);
  }

  SECTION("M name the boundary in the content type W it is made") {
    const MultipartBoundary random = MultipartBoundary::Random();

    const std::string expected =
        "multipart/form-data; boundary=" + std::string(random.Boundary());
    REQUIRE(random.ContentType() == expected);
  }

  SECTION("M make a different boundary W another is made") {
    const MultipartBoundary first = MultipartBoundary::Random();
    const MultipartBoundary second = MultipartBoundary::Random();
    REQUIRE(first.Boundary() != second.Boundary());
  }

  SECTION("M keep the same boundary W it is copied") {
    const MultipartBoundary original = MultipartBoundary::Random();
    const MultipartBoundary copy = original;
    REQUIRE(copy.Boundary() == original.Boundary());
    REQUIRE(copy.ContentType() == original.ContentType());
  }
}
