// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/http/body_writer_string.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "datadog/impl/core/feature_read.hpp"
#include "datadog/impl/core/http/client.hpp"
#include "datadog/impl/core/tlv.hpp"

#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("StringWriter", "[unit]") {
  SECTION("M write string to buffer W called as functor") {
    // Given a StringWriter initialized from a string of length 8
    std::string s{"deadbeef"};
    HttpBodyWriter writer = StringWriter{s};

    // When the string is written to a buffer of size 8
    char buffer[8];
    const size_t num_bytes_written = writer(buffer, 8);

    // Then those 8 bytes are copied exactly
    REQUIRE(num_bytes_written == 8);
    REQUIRE(std::strstr(buffer, "deadbeef") == buffer);
  }

  SECTION("M not include null terminator W called as functor") {
    // Given a StringWriter initialized from a string of length 8
    std::string s{"deadbeef"};
    HttpBodyWriter writer = StringWriter{s};

    // When the string is written to a buffer of size 9
    char buffer[9];
    std::memset(buffer, 'z', 9);
    const size_t num_bytes_written = writer(buffer, 9);

    // Then only 8 bytes are written, without null terminator
    REQUIRE(num_bytes_written == 8);
    REQUIRE(std::memcmp(buffer, "deadbeefz", 9) == 0);
  }

  SECTION("M write in multiple parts W buffer size is small") {
    // Given a StringWriter initialized from a string of length 8
    std::string s{"deadbeef"};
    HttpBodyWriter writer = StringWriter{s};

    // When the string is written to a buffer given a dst size of 4
    char buffer[9];
    std::memset(buffer, '_', 9);
    size_t num_bytes_written = writer(buffer, 4);

    // Then only 4 bytes are written, without null terminator
    REQUIRE(num_bytes_written == 4);
    REQUIRE(std::memcmp(buffer, "dead_____", 9) == 0);

    // And: When another write is performed with the same size
    num_bytes_written = writer(buffer + 4, 4);

    // Then the writer picks up where it left off
    REQUIRE(num_bytes_written == 4);
    REQUIRE(std::memcmp(buffer, "deadbeef_", 9) == 0);
  }

  SECTION("M return EOF W finished writing") {
    // Given a StringWriter that's written all it can write
    std::string s{"deadbeef"};
    HttpBodyWriter writer = StringWriter{s};
    char buffer[8];
    size_t num_bytes_written = writer(buffer, 8);
    REQUIRE(num_bytes_written == 8);

    // When write is called again
    num_bytes_written = writer(buffer, 8);

    // Then nothing is written, and 0 signals EOF
    REQUIRE(num_bytes_written == 0);
  }
}
