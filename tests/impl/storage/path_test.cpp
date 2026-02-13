// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/path.hpp"

#include <cstring>

#include "support/catch.hpp"

using namespace datadog::impl;

TEST_CASE("StoragePath", "[unit][storage]") {
  SECTION("M return empty string_view W Get is called on uninitialized") {
    // Given a StoragePath value that is not explicitly populated
    StoragePath path;

    // When we call Get() to retrieve the held value as std::string_view
    std::string_view got = path.Get();

    // Then the resulting value is empty
    REQUIRE(got.empty());
  }

  SECTION("M return valid pointer to empty string W CStr is called on uninitialized") {
    // Given a StoragePath value that is not explicitly populated
    StoragePath path;

    // When we call CStr() to retrieve the held value as a const char*
    const char* got = path.CStr();

    // Then the resulting value is a valid pointer to the buffer held by the
    // StoragePath, and that buffer contains no string value
    REQUIRE(got != nullptr);
    REQUIRE(got[0] == '\0');
  }

  SECTION("M populate path W Set is called with short enough value") {
    // Given a StoragePath value
    StoragePath path;

    // When we call Set() with a reasonably-sized string
    const bool ok = path.Set("/foo/bar/baz");

    // Then the operation succeeds
    REQUIRE(ok);

    // And the value returned by Get()/CStr() reflects the value we set
    REQUIRE(path.Get() == "/foo/bar/baz");
    REQUIRE(std::strcmp(path.CStr(), "/foo/bar/baz") == 0);
  }

  SECTION("M overwrite held value W Set is successful") {
    // Given a StoragePath value that currently holds /foo/bar/baz
    StoragePath path;
    REQUIRE(path.Set("/foo/bar/baz"));

    // When we call Set() with a different value that can be held successfully
    const bool ok = path.Set("/tmp/hi");
    REQUIRE(ok);

    // Then the value returned by Get()/CStr() reflects the new value
    REQUIRE(path.Get() == "/tmp/hi");
    REQUIRE(std::strcmp(path.CStr(), "/tmp/hi") == 0);
  }

  SECTION("M store empty string W Set is called with empty string") {
    // Given a StoragePath value that currently holds /foo/bar/baz
    StoragePath path;
    REQUIRE(path.Set("/foo/bar/baz"));

    // When we call Set() with an empty string
    const bool ok = path.Set("");
    REQUIRE(ok);

    // Then the value returned by Get()/CStr() is now a valid empty string
    REQUIRE(path.Get().empty());
    REQUIRE(path.CStr() != nullptr);
    REQUIRE(path.CStr()[0] == '\0');
  }

  SECTION("M fail and leave value intact W Set is called with parent-relative path") {
    // Given a StoragePath value that currently holds /foo/bar/baz
    StoragePath path;
    REQUIRE(path.Set("/foo/bar/baz"));

    // When we call Set() with a string that contains ".."
    const bool ok = path.Set("../../../foo");

    // Then the operation fails
    REQUIRE(!ok);

    // And the value returned by Get()/CStr() reflects the original, unchanged value
    REQUIRE(path.Get() == "/foo/bar/baz");
    REQUIRE(std::strcmp(path.CStr(), "/foo/bar/baz") == 0);
  }

  SECTION("M fail and leave value intact W Set is called with too-long value") {
    // Given a StoragePath value that currently holds /foo/bar/baz
    StoragePath path;
    REQUIRE(path.Set("/foo/bar/baz"));

    // When we call Set() with a value that exceeds MAX_STORAGE_PATH_SIZE
    const bool ok = path.Set(
        "/tmp/aaaaaaaaaaaaaaaa/aaeaaaaaaaaaaaaa/ab2aaaaaaaaaaaaa/ab6aaaaaaaaaaaaa/"
        "abaaaaaaaaaaaaaa/abeaaaaaaaaaaaaa/ac2aaaaaaaaaaaaa/ac6aaaaaaaaaaaaa/"
        "acaaaaaaaaaaaaaa/aceaaaaaaaaaaaaa/ad2aaaaaaaaaaaaa/ad6aaaaaaaaaaaaa/"
        "adaaaaaaaaaaaaaa/adeaaaaaaaaaaaaa/ae2aaaaaaaaaaaaa/ae6aaaaaaaaaaaaa/"
        "aeaaaaaaaaaaaaaa/aeeaaaaaaaaaaaaa/af2aaaaaaaaaaaaa/af6aaaaaaaaaaaaa/"
        "afaaaaaaaaaaaaaa/afeaaaaaaaaaaaaa/b02aaaaaaaaaaaaa/b06aaaaaaaaaaaaa/"
        "b0aaaaaaaaaaaaaa/b0eaaaaaaaaaaaaa/b12aaaaaaaaaaaaa/b16aaaaaaaaaaaaa/"
        "b1aaaaaaaaaaaaaa/b1eaaaaaaaaaaaaa/b22aaaaaaaaaaaaa/b26aaaaaaaaaaaaa/"
        "b2aaaaaaaaaaaaaa/b2eaaaaaaaaaaaaa/b32aaaaaaaaaaaaa/b36aaaaaaaaaaaaa/"
        "b3aaaaaaaaaaaaaa/b3eaaaaaaaaaaaaa/b42aaaaaaaaaaaaa/b46aaaaaaaaaaaaa/"
        "b4aaaaaaaaaaaaaa/b4eaaaaaaaaaaaaa/b52aaaaaaaaaaaaa/b56aaaaaaaaaaaaa/"
        "b5aaaaaaaaaaaaaa/b5eaaaaaaaaaaaaa/b62aaaaaaaaaaaaa/b66aaaaaaaaaaaaa/"
        "b6aaaaaaaaaaaaaa/b6eaaaaaaaaaaaaa/b72aaaaaaaaaaaaa/b76aaaaaaaaaaaaa/"
        "b7aaaaaaaaaaaaaa/b7eaaaaaaaaaaaaa/b82aaaaaaaaaaaaa/b86aaaaaaaaaaaaa/"
        "b8aaaaaaaaaaaaaa/b8eaaaaaaaaaaaaa/b92aaaaaaaaaaaaa/b96aaaaaaaaaaaaa/"
        "b9aaaaaaaaaaaaaa/b9eaaaaaaaaaaaaa/ba2aaaaaaaaaaaaa/ba6aaaaaaaaaaaaa/"
        "baaaaaaaaaaaaaaa/baeaaaaaaaaaaaaa/bb2aaaaaaaaaaaaa/bb6aaaaaaaaaaaaa/"
        "bbaaaaaaaaaaaaaa/bbeaaaaaaaaaaaaa/bc2aaaaaaaaaaaaa/bc6aaaaaaaaaaaaa/"
        "bcaaaaaaaaaaaaaa/bceaaaaaaaaaaaaa/bd2aaaaaaaaaaaaa/bd6aaaaaaaaaaaaa/"
        "bdaaaaaaaaaaaaaa/bdeaaaaaaaaaaaaa/be2aaaaaaaaaaaaa/be6aaaaaaaaaaaaa/"
        "beaaaaaaaaaaaaaa/beeaaaaaaaaaaaaa/bf2aaaaaaaaaaaaa/bf6aaaaaaaaaaaaa/"
        "bfaaaaaaaaaaaaaa/bfeaaaaaaaaaaaaa/c02aaaaaaaaaaaaa/c06aaaaaaaaaaaaa/"
        "c0aaaaaaaaaaaaaa/c0eaaaaaaaaaaaaa/c12aaaaaaaaaaaaa/c16aaaaaaaaaaaaa/"
        "c1aaaaaaaaaaaaaa/c1eaaaaaaaaaaaaa/c22aaaaaaaaaaaaa/c26aaaaaaaaaaaaa/"
        "c2aaaaaaaaaaaaaa/c2eaaaaaaaaaaaaa/c32aaaaaaaaaaaaa/c36aaaaaaaaaaaaa/"
        "c3aaaaaaaaaaaaaa/c3eaaaaaaaaaaaaa/c42aaaaaaaaaaaaa/c46aaaaaaaaaaaaa/"
        "c4aaaaaaaaaaaaaa/c4eaaaaaaaaaaaaa/c52aaaaaaaaaaaaa/c56aaaaaaaaaaaaa/"
        "c5aaaaaaaaaaaaaa/c5eaaaaaaaaaaaaa/c62aaaaaaaaaaaaa/c66aaaaaaaaaaaaa/"
        "c6aaaaaaaaaaaaaa/c6eaaaaaaaaaaaaa/c72aaaaaaaaaaaaa/c76aaaaaaaaaaaaa/"
        "c7aaaaaaaaaaaaaa/c7eaaaaaaaaaaaaa/c82aaaaaaaaaaaaa/c86aaaaaaaaaaaaa/"
        "c8aaaaaaaaaaaaaa/c8eaaaaaaaaaaaaa/c92aaaaaaaaaaaaa/c96aaaaaaaaaaaaa/"
        "c9aaaaaaaaaaaaaa/c9eaaaaaaaaaaaaa/ca2aaaaaaaaaaaaa/ca6aaaaaaaaaaaaa/"
        "caaaaaaaaaaaaaaa/caeaaaaaaaaaaaaa/cb2aaaaaaaaaaaaa/cb6aaaaaaaaaaaaa/"
        "cbaaaaaaaaaaaaaa/cbeaaaaaaaaaaaaa/cc2aaaaaaaaaaaaa/cc6aaaaaaaaaaaaa/"
        "ccaaaaaaaaaaaaaa/cceaaaaaaaaaaaaa/cd2aaaaaaaaaaaaa/cd6aaaaaaaaaaaaa/"
        "cdaaaaaaaaaaaaaa/cdeaaaaaaaaaaaaa/ce2aaaaaaaaaaaaa/ce6aaaaaaaaaaaaa/"
        "ceaaaaaaaaaaaaaa/ceeaaaaaaaaaaaaa/cf2aaaaaaaaaaaaa/cf6aaaaaaaaaaaaa/"
        "cfaaaaaaaaaaaaaa/cfeaaaaaaaaaaaaa/d02aaaaaaaaaaaaa/d06aaaaaaaaaaaaa/"
        "d0aaaaaaaaaaaaaa/d0eaaaaaaaaaaaaa/d12aaaaaaaaaaaaa/d16aaaaaaaaaaaaa/"
        "d1aaaaaaaaaaaaaa/d1eaaaaaaaaaaaaa/d22aaaaaaaaaaaaa/d26aaaaaaaaaaaaa/"
        "d2aaaaaaaaaaaaaa/d2eaaaaaaaaaaaaa/d32aaaaaaaaaaaaa/d36aaaaaaaaaaaaa/"
        "d3aaaaaaaaaaaaaa/d3eaaaaaaaaaaaaa/d42aaaaaaaaaaaaa/d46aaaaaaaaaaaaa/"
        "d4aaaaaaaaaaaaaa/d4eaaaaaaaaaaaaa/d52aaaaaaaaaaaaa/d56aaaaaaaaaaaaa/"
        "d5aaaaaaaaaaaaaa/d5eaaaaaaaaaaaaa/d62aaaaaaaaaaaaa/d66aaaaaaaaaaaaa/"
        "d6aaaaaaaaaaaaaa/d6eaaaaaaaaaaaaa/d72aaaaaaaaaaaaa/d76aaaaaaaaaaaaa/"
        "d7aaaaaaaaaaaaaa/d7eaaaaaaaaaaaaa/d82aaaaaaaaaaaaa/d86aaaaaaaaaaaaa/"
        "d8aaaaaaaaaaaaaa/d8eaaaaaaaaaaaaa/d92aaaaaaaaaaaaa/d96aaaaaaaaaaaaa/"
        "d9aaaaaaaaaaaaaa/d9eaaaaaaaaaaaaa/da2aaaaaaaaaaaaa/da6aaaaaaaaaaaaa/"
        "daaaaaaaaaaaaaaa/daeaaaaaaaaaaaaa/db2aaaaaaaaaaaaa/db6aaaaaaaaaaaaa/"
        "dbaaaaaaaaaaaaaa/dbeaaaaaaaaaaaaa/dc2aaaaaaaaaaaaa/dc6aaaaaaaaaaaaa/"
        "dcaaaaaaaaaaaaaa/dceaaaaaaaaaaaaa/dd2aaaaaaaaaaaaa/dd6aaaaaaaaaaaaa/"
        "ddaaaaaaaaaaaaaa/ddeaaaaaaaaaaaaa/de2aaaaaaaaaaaaa/de6aaaaaaaaaaaaa/"
        "deaaaaaaaaaaaaaa/deeaaaaaaaaaaaaa/df2aaaaaaaaaaaaa/df6aaaaaaaaaaaaa/"
        "dfaaaaaaaaaaaaaa/dfeaaaaaaaaaaaaa/e02aaaaaaaaaaaaa/e06aaaaaaaaaaaaa/"
        "e0aaaaaaaaaaaaaa/e0eaaaaaaaaaaaaa/e12aaaaaaaaaaaaa/e16aaaaaaaaaaaaa/"
        "e1aaaaaaaaaaaaaa/e1eaaaaaaaaaaaaa/e22aaaaaaaaaaaaa/e26aaaaaaaaaaaaa/"
        "e2aaaaaaaaaaaaaa/e2eaaaaaaaaaaaaa/e32aaaaaaaaaaaaa/e36aaaaaaaaaaaaa/"
        "e3aaaaaaaaaaaaaa/e3eaaaaaaaaaaaaa/e42aaaaaaaaaaaaa/e46aaaaaaaaaaaaa/"
        "e4aaaaaaaaaaaaaa/e4eaaaaaaaaaaaaa/e52aaaaaaaaaaaaa/e56aaaaaaaaaaaaa/"
        "e5aaaaaaaaaaaaaa/e5eaaaaaaaaaaaaa/e62aaaaaaaaaaaaa/e66aaaaaaaaaaaaa/"
        "e6aaaaaaaaaaaaaa/e6eaaaaaaaaaaaaa/e72aaaaaaaaaaaaa/e76aaaaaaaaaaaaa/"
        "e7aaaaaaaaaaaaaa/e7eaaaaaaaaaaaaa/e82aaaaaaaaaaaaa/e86aaaaaaaaaaaaa/"
        "e8aaaaaaaaaaaaaa/e8eaaaaaaaaaaaaa/e92aaaaaaaaaaaaa/e96aaaaaaaaaaaaa/"
        "e9aaaaaaaaaaaaaa/e9eaaaaaaaaaaaaa/ea2aaaaaaaaaaaaa/ea6aaaaaaaaaaaaa/"
        "eaaaaaaaaaaaaaaa/eaeaaaaaaaaaaaaa/eb2aaaaaaaaaaaaa/eb6aaaaaaaaaaaaa/"
        "ebaaaaaaaaaaaaaa/ebeaaaaaaaaaaaaa/ec2aaaaaaaaaaaaa/ec6aaaaaaaaaaaaa/"
        "ecaaaaaaaaaaaaaa/eceaaaaaaaaaaaaa/ed2aaaaaaaaaaaaa/ed6aaaaaaaaaaaaa/"
        "edaaaaaaaaaaaaaa/edeaaaaaaaaaaaaa/ee2aaaaaaaaaaaaa/ee6aaaaaaaaaaaaa/"
        "eeaaaaaaaaaaaaaa/eeeaaaaaaaaaaaaa/ef2aaaaaaaaaaaaa/ef6aaaaaaaaaaaaa/"
        "efaaaaaaaaaaaaaa/efeaaaaaaaaaaaaa/f02aaaaaaaaaaaaa/f06aaaaaaaaaaaaa/"
        "f0aaaaaaaaaaaaaa/f0eaaaaaaaaaaaaa/f12aaaaaaaaaaaaa/f16aaaaaaaaaaaaa/"
        "f1aaaaaaaaaaaaaa/f1eaaaaaaaaaaaaa/f22aaaaaaaaaaaaa/f26aaaaaaaaaaaaa/"
        "f2aaaaaaaaaaaaaa/f2eaaaaaaaaaaaaa/f32aaaaaaaaaaaaa/f36aaaaaaaaaaaaa/"
        "f3aaaaaaaaaaaaaa/f3eaaaaaaaaaaaaa/f42aaaaaaaaaaaaa/f46aaaaaaaaaaaaa/"
        "f4aaaaaaaaaaaaaa/f4eaaaaaaaaaaaaa/f52aaaaaaaaaaaaa/f56aaaaaaaaaaaaa/"
        "f5aaaaaaaaaaaaaa/f5eaaaaaaaaaaaaa/f62aaaaaaaaaaaaa/f66aaaaaaaaaaaaa/"
        "f6aaaaaaaaaaaaaa/f6eaaaaaaaaaaaaa/f72aaaaaaaaaaaaa/f76aaaaaaaaaaaaa/"
        "f7aaaaaaaaaaaaaa/f7eaaaaaaaaaaaaa/f82aaaaaaaaaaaaa/f86aaaaaaaaaaaaa/"
        "f8aaaaaaaaaaaaaa/f8eaaaaaaaaaaaaa/f92aaaaaaaaaaaaa/f96aaaaaaaaaaaaa/"
        "f9aaaaaaaaaaaaaa/f9eaaaaaaaaaaaaa/fa2aaaaaaaaaaaaa/fa6aaaaaaaaaaaaa/"
        "faaaaaaaaaaaaaaa/faeaaaaaaaaaaaaa/fb2aaaaaaaaaaaaa/fb6aaaaaaaaaaaaa/"
        "fbaaaaaaaaaaaaaa/fbeaaaaaaaaaaaaa/fc2aaaaaaaaaaaaa/fc6aaaaaaaaaaaaa/"
        "fcaaaaaaaaaaaaaa/fceaaaaaaaaaaaaa/fd2aaaaaaaaaaaaa/fd6aaaaaaaaaaaaa/"
        "fdaaaaaaaaaaaaaa/fdeaaaaaaaaaaaaa/fe2aaaaaaaaaaaaa/fe6aaaaaaaaaaaaa/"
        "feaaaaaaaaaaaaaa/feeaaaaaaaaaaaaa/ff2aaaaaaaaaaaaa/ff6aaaaaaaaaaaaa/"
        "ffaaaaaaaaaaaaaa/ffeaaaaaaaaaaaaa"
    );

    // Then the operation fails
    REQUIRE(!ok);

    // And the value returned by Get()/CStr() reflects the original, unchanged value
    REQUIRE(path.Get() == "/foo/bar/baz");
    REQUIRE(std::strcmp(path.CStr(), "/foo/bar/baz") == 0);
  }

  SECTION("M concatenate paths W Join is called with valid parent path") {
    // Given '/tmp/foo' + 'bar', resulting path joins both with OS-specific slash char
    StoragePath path;
    REQUIRE(path.Join("/tmp/foo", "bar"));
#ifdef _WIN32
    REQUIRE(path.Get() == "/tmp/foo\\bar");
#else
    REQUIRE(path.Get() == "/tmp/foo/bar");
#endif
  }

  SECTION("M concatenate paths W Append is called with valid parent path") {
    // Given '/tmp/foo' + 'bar', resulting path joins both with OS-specific slash char
    StoragePath path;
    REQUIRE(path.Set("/tmp/foo"));
    REQUIRE(path.Append("bar"));
#ifdef _WIN32
    REQUIRE(path.Get() == "/tmp/foo\\bar");
#else
    REQUIRE(path.Get() == "/tmp/foo/bar");
#endif
  }

  SECTION("M handle trailing slash on parent path W Join is called") {
    // Given '/tmp/foo/' + 'bar', resulting path joins both with OS-specific slash char,
    // maintaining trailing slash from parent dir without adding a redundant slash
    StoragePath path;
    REQUIRE(path.Join("/tmp/foo/", "bar"));
    REQUIRE(path.Get() == "/tmp/foo/bar");
  }

  SECTION("M handle trailing slash on parent path W Append is called") {
    // Given '/tmp/foo/' + 'bar', resulting path joins both with OS-specific slash char,
    // maintaining trailing slash from parent dir without adding a redundant slash
    StoragePath path;
    REQUIRE(path.Set("/tmp/foo/"));
    REQUIRE(path.Append("bar"));
    REQUIRE(path.Get() == "/tmp/foo/bar");
  }

#ifdef _WIN32
  SECTION("M treat backslash as path delimiter W Join is called") {
    // On Windows, a trailing backslash is recognized as a delimiter
    StoragePath path;
    REQUIRE(path.Join("C:\\Temp\\foo\\", "bar"));
    REQUIRE(path.Get() == "C:\\Temp\\foo\\bar");
  }
#else
  SECTION("M treat backlash as ordinary filename character W Join is called") {
    // On all other platforms, a backslash has no special meaning
    StoragePath path;
    REQUIRE(path.Join("/tmp/foo\\", "bar"));
    REQUIRE(path.Get() == "/tmp/foo\\/bar");
  }
#endif

#ifdef _WIN32
  SECTION("M treat backslash as path delimiter W Append is called") {
    // On Windows, a trailing backslash is recognized as a delimiter
    StoragePath path;
    REQUIRE(path.Set("C:\\Temp\\foo\\"));
    REQUIRE(path.Append("bar"));
    REQUIRE(path.Get() == "C:\\Temp\\foo\\bar");
  }
#else
  SECTION("M treat backlash as ordinary filename character W Append is called") {
    // On all other platforms, a backslash has no special meaning
    StoragePath path;
    REQUIRE(path.Set("/tmp/foo\\"));
    REQUIRE(path.Append("bar"));
    REQUIRE(path.Get() == "/tmp/foo\\/bar");
  }
#endif

  SECTION("M build relative path W Join is called with empty parent path") {
    StoragePath path;
    REQUIRE(path.Join("", "foo"));
    REQUIRE(path.Get() == "foo");
  }

  SECTION("M build relative path W Append is called with empty parent path") {
    StoragePath path;
    REQUIRE(path.Append("foo"));
    REQUIRE(path.Get() == "foo");
  }

  SECTION("M build relative path W Join is called with dot as parent path") {
    StoragePath path;
    REQUIRE(path.Join(".", "foo"));
    REQUIRE(path.Get() == "foo");
  }

  SECTION("M build relative path W Join is called with dot as parent path") {
    StoragePath path;
    REQUIRE(path.Set("."));
    REQUIRE(path.Append("foo"));
    REQUIRE(path.Get() == "foo");
  }

  SECTION("M fail W Join is called with relative parent path") {
    StoragePath path;
    const bool ok = path.Join("../..", "foo");
    REQUIRE(!ok);
  }

  SECTION("M fail W Join is called with name that contains double-dot") {
#ifdef WIN32
    auto value = GENERATE(
        "..", "../..", "../bar", "bar/..", "..\\..", "..\\bar", "bar\\..", "a..z"
    );
#else
    auto value = GENERATE("..", "../..", "../bar", "bar/..", "a..z");
#endif
    StoragePath path;
    const bool ok = path.Join("/tmp/foo", value);
    REQUIRE(!ok);
  }

  SECTION("M fail W Append is called with name that contains double-dot") {
#ifdef WIN32
    auto value = GENERATE(
        "..", "../..", "../bar", "bar/..", "..\\..", "..\\bar", "bar\\..", "a..z"
    );
#else
    auto value = GENERATE("..", "../..", "../bar", "bar/..", "a..z");
#endif
    StoragePath path;
    REQUIRE(path.Set("/tmp/foo"));
    const bool ok = path.Append(value);
    REQUIRE(!ok);
  }

  SECTION("M fail and leave value intact W Join is called with too-long value") {
    // Given a StoragePath value that currently holds /foo/bar/baz
    StoragePath path;
    REQUIRE(path.Set("/foo/bar/baz"));

    // When we call Join() with values that collectively exceed MAX_STORAGE_PATH_SIZE
    const bool ok = path.Join(
        "/tmp/aaaaaaaaaaaaaaaa/aaeaaaaaaaaaaaaa/ab2aaaaaaaaaaaaa/ab6aaaaaaaaaaaaa/"
        "abaaaaaaaaaaaaaa/abeaaaaaaaaaaaaa/ac2aaaaaaaaaaaaa/ac6aaaaaaaaaaaaa/"
        "acaaaaaaaaaaaaaa/aceaaaaaaaaaaaaa/ad2aaaaaaaaaaaaa/ad6aaaaaaaaaaaaa/"
        "adaaaaaaaaaaaaaa/adeaaaaaaaaaaaaa/ae2aaaaaaaaaaaaa/ae6aaaaaaaaaaaaa/"
        "aeaaaaaaaaaaaaaa/aeeaaaaaaaaaaaaa/af2aaaaaaaaaaaaa/af6aaaaaaaaaaaaa/"
        "afaaaaaaaaaaaaaa/afeaaaaaaaaaaaaa/b02aaaaaaaaaaaaa/b06aaaaaaaaaaaaa/"
        "b0aaaaaaaaaaaaaa/b0eaaaaaaaaaaaaa/b12aaaaaaaaaaaaa/b16aaaaaaaaaaaaa/"
        "b1aaaaaaaaaaaaaa/b1eaaaaaaaaaaaaa/b22aaaaaaaaaaaaa/b26aaaaaaaaaaaaa/"
        "b2aaaaaaaaaaaaaa/b2eaaaaaaaaaaaaa/b32aaaaaaaaaaaaa/b36aaaaaaaaaaaaa/"
        "b3aaaaaaaaaaaaaa/b3eaaaaaaaaaaaaa/b42aaaaaaaaaaaaa/b46aaaaaaaaaaaaa/"
        "b4aaaaaaaaaaaaaa/b4eaaaaaaaaaaaaa/b52aaaaaaaaaaaaa/b56aaaaaaaaaaaaa/"
        "b5aaaaaaaaaaaaaa/b5eaaaaaaaaaaaaa/b62aaaaaaaaaaaaa/b66aaaaaaaaaaaaa/"
        "b6aaaaaaaaaaaaaa/b6eaaaaaaaaaaaaa/b72aaaaaaaaaaaaa/b76aaaaaaaaaaaaa/"
        "b7aaaaaaaaaaaaaa/b7eaaaaaaaaaaaaa/b82aaaaaaaaaaaaa/b86aaaaaaaaaaaaa/"
        "b8aaaaaaaaaaaaaa/b8eaaaaaaaaaaaaa/b92aaaaaaaaaaaaa/b96aaaaaaaaaaaaa/"
        "b9aaaaaaaaaaaaaa/b9eaaaaaaaaaaaaa/ba2aaaaaaaaaaaaa/ba6aaaaaaaaaaaaa/"
        "baaaaaaaaaaaaaaa/baeaaaaaaaaaaaaa/bb2aaaaaaaaaaaaa/bb6aaaaaaaaaaaaa/"
        "bbaaaaaaaaaaaaaa/bbeaaaaaaaaaaaaa/bc2aaaaaaaaaaaaa/bc6aaaaaaaaaaaaa/"
        "bcaaaaaaaaaaaaaa/bceaaaaaaaaaaaaa/bd2aaaaaaaaaaaaa/bd6aaaaaaaaaaaaa/"
        "bdaaaaaaaaaaaaaa/bdeaaaaaaaaaaaaa/be2aaaaaaaaaaaaa/be6aaaaaaaaaaaaa/"
        "beaaaaaaaaaaaaaa/beeaaaaaaaaaaaaa/bf2aaaaaaaaaaaaa/bf6aaaaaaaaaaaaa/"
        "bfaaaaaaaaaaaaaa/bfeaaaaaaaaaaaaa/c02aaaaaaaaaaaaa/c06aaaaaaaaaaaaa",
        "c0aaaaaaaaaaaaaa/c0eaaaaaaaaaaaaa/c12aaaaaaaaaaaaa/c16aaaaaaaaaaaaa/"
        "c1aaaaaaaaaaaaaa/c1eaaaaaaaaaaaaa/c22aaaaaaaaaaaaa/c26aaaaaaaaaaaaa/"
        "c2aaaaaaaaaaaaaa/c2eaaaaaaaaaaaaa/c32aaaaaaaaaaaaa/c36aaaaaaaaaaaaa/"
        "c3aaaaaaaaaaaaaa/c3eaaaaaaaaaaaaa/c42aaaaaaaaaaaaa/c46aaaaaaaaaaaaa/"
        "c4aaaaaaaaaaaaaa/c4eaaaaaaaaaaaaa/c52aaaaaaaaaaaaa/c56aaaaaaaaaaaaa/"
        "c5aaaaaaaaaaaaaa/c5eaaaaaaaaaaaaa/c62aaaaaaaaaaaaa/c66aaaaaaaaaaaaa/"
        "c6aaaaaaaaaaaaaa/c6eaaaaaaaaaaaaa/c72aaaaaaaaaaaaa/c76aaaaaaaaaaaaa/"
        "c7aaaaaaaaaaaaaa/c7eaaaaaaaaaaaaa/c82aaaaaaaaaaaaa/c86aaaaaaaaaaaaa/"
        "c8aaaaaaaaaaaaaa/c8eaaaaaaaaaaaaa/c92aaaaaaaaaaaaa/c96aaaaaaaaaaaaa/"
        "c9aaaaaaaaaaaaaa/c9eaaaaaaaaaaaaa/ca2aaaaaaaaaaaaa/ca6aaaaaaaaaaaaa/"
        "caaaaaaaaaaaaaaa/caeaaaaaaaaaaaaa/cb2aaaaaaaaaaaaa/cb6aaaaaaaaaaaaa/"
        "cbaaaaaaaaaaaaaa/cbeaaaaaaaaaaaaa/cc2aaaaaaaaaaaaa/cc6aaaaaaaaaaaaa/"
        "ccaaaaaaaaaaaaaa/cceaaaaaaaaaaaaa/cd2aaaaaaaaaaaaa/cd6aaaaaaaaaaaaa/"
        "cdaaaaaaaaaaaaaa/cdeaaaaaaaaaaaaa/ce2aaaaaaaaaaaaa/ce6aaaaaaaaaaaaa/"
        "ceaaaaaaaaaaaaaa/ceeaaaaaaaaaaaaa/cf2aaaaaaaaaaaaa/cf6aaaaaaaaaaaaa/"
        "cfaaaaaaaaaaaaaa/cfeaaaaaaaaaaaaa/d02aaaaaaaaaaaaa/d06aaaaaaaaaaaaa/"
        "d0aaaaaaaaaaaaaa/d0eaaaaaaaaaaaaa/d12aaaaaaaaaaaaa/d16aaaaaaaaaaaaa/"
        "d1aaaaaaaaaaaaaa/d1eaaaaaaaaaaaaa/d22aaaaaaaaaaaaa/d26aaaaaaaaaaaaa/"
        "d2aaaaaaaaaaaaaa/d2eaaaaaaaaaaaaa/d32aaaaaaaaaaaaa/d36aaaaaaaaaaaaa/"
        "d3aaaaaaaaaaaaaa/d3eaaaaaaaaaaaaa/d42aaaaaaaaaaaaa/d46aaaaaaaaaaaaa/"
        "d4aaaaaaaaaaaaaa/d4eaaaaaaaaaaaaa/d52aaaaaaaaaaaaa/d56aaaaaaaaaaaaa/"
        "d5aaaaaaaaaaaaaa/d5eaaaaaaaaaaaaa/d62aaaaaaaaaaaaa/d66aaaaaaaaaaaaa/"
        "d6aaaaaaaaaaaaaa/d6eaaaaaaaaaaaaa/d72aaaaaaaaaaaaa/d76aaaaaaaaaaaaa/"
        "d7aaaaaaaaaaaaaa/d7eaaaaaaaaaaaaa/d82aaaaaaaaaaaaa/d86aaaaaaaaaaaaa/"
        "d8aaaaaaaaaaaaaa/d8eaaaaaaaaaaaaa/d92aaaaaaaaaaaaa/d96aaaaaaaaaaaaa/"
        "d9aaaaaaaaaaaaaa/d9eaaaaaaaaaaaaa/da2aaaaaaaaaaaaa/da6aaaaaaaaaaaaa/"
        "daaaaaaaaaaaaaaa/daeaaaaaaaaaaaaa/db2aaaaaaaaaaaaa/db6aaaaaaaaaaaaa/"
        "dbaaaaaaaaaaaaaa/dbeaaaaaaaaaaaaa/dc2aaaaaaaaaaaaa/dc6aaaaaaaaaaaaa/"
        "dcaaaaaaaaaaaaaa/dceaaaaaaaaaaaaa/dd2aaaaaaaaaaaaa/dd6aaaaaaaaaaaaa/"
        "ddaaaaaaaaaaaaaa/ddeaaaaaaaaaaaaa/de2aaaaaaaaaaaaa/de6aaaaaaaaaaaaa/"
        "deaaaaaaaaaaaaaa/deeaaaaaaaaaaaaa/df2aaaaaaaaaaaaa/df6aaaaaaaaaaaaa/"
        "dfaaaaaaaaaaaaaa/dfeaaaaaaaaaaaaa/e02aaaaaaaaaaaaa/e06aaaaaaaaaaaaa/"
        "e0aaaaaaaaaaaaaa/e0eaaaaaaaaaaaaa/e12aaaaaaaaaaaaa/e16aaaaaaaaaaaaa/"
        "e1aaaaaaaaaaaaaa/e1eaaaaaaaaaaaaa/e22aaaaaaaaaaaaa/e26aaaaaaaaaaaaa/"
        "e2aaaaaaaaaaaaaa/e2eaaaaaaaaaaaaa/e32aaaaaaaaaaaaa/e36aaaaaaaaaaaaa/"
        "e3aaaaaaaaaaaaaa/e3eaaaaaaaaaaaaa/e42aaaaaaaaaaaaa/e46aaaaaaaaaaaaa/"
        "e4aaaaaaaaaaaaaa/e4eaaaaaaaaaaaaa/e52aaaaaaaaaaaaa/e56aaaaaaaaaaaaa/"
        "e5aaaaaaaaaaaaaa/e5eaaaaaaaaaaaaa/e62aaaaaaaaaaaaa/e66aaaaaaaaaaaaa/"
        "e6aaaaaaaaaaaaaa/e6eaaaaaaaaaaaaa/e72aaaaaaaaaaaaa/e76aaaaaaaaaaaaa/"
        "e7aaaaaaaaaaaaaa/e7eaaaaaaaaaaaaa/e82aaaaaaaaaaaaa/e86aaaaaaaaaaaaa/"
        "e8aaaaaaaaaaaaaa/e8eaaaaaaaaaaaaa/e92aaaaaaaaaaaaa/e96aaaaaaaaaaaaa/"
        "e9aaaaaaaaaaaaaa/e9eaaaaaaaaaaaaa/ea2aaaaaaaaaaaaa/ea6aaaaaaaaaaaaa/"
        "eaaaaaaaaaaaaaaa/eaeaaaaaaaaaaaaa/eb2aaaaaaaaaaaaa/eb6aaaaaaaaaaaaa/"
        "ebaaaaaaaaaaaaaa/ebeaaaaaaaaaaaaa/ec2aaaaaaaaaaaaa/ec6aaaaaaaaaaaaa/"
        "ecaaaaaaaaaaaaaa/eceaaaaaaaaaaaaa/ed2aaaaaaaaaaaaa/ed6aaaaaaaaaaaaa/"
        "edaaaaaaaaaaaaaa/edeaaaaaaaaaaaaa/ee2aaaaaaaaaaaaa/ee6aaaaaaaaaaaaa/"
        "eeaaaaaaaaaaaaaa/eeeaaaaaaaaaaaaa/ef2aaaaaaaaaaaaa/ef6aaaaaaaaaaaaa/"
        "efaaaaaaaaaaaaaa/efeaaaaaaaaaaaaa/f02aaaaaaaaaaaaa/f06aaaaaaaaaaaaa/"
        "f0aaaaaaaaaaaaaa/f0eaaaaaaaaaaaaa/f12aaaaaaaaaaaaa/f16aaaaaaaaaaaaa/"
        "f1aaaaaaaaaaaaaa/f1eaaaaaaaaaaaaa/f22aaaaaaaaaaaaa/f26aaaaaaaaaaaaa/"
        "f2aaaaaaaaaaaaaa/f2eaaaaaaaaaaaaa/f32aaaaaaaaaaaaa/f36aaaaaaaaaaaaa/"
        "f3aaaaaaaaaaaaaa/f3eaaaaaaaaaaaaa/f42aaaaaaaaaaaaa/f46aaaaaaaaaaaaa/"
        "f4aaaaaaaaaaaaaa/f4eaaaaaaaaaaaaa/f52aaaaaaaaaaaaa/f56aaaaaaaaaaaaa/"
        "f5aaaaaaaaaaaaaa/f5eaaaaaaaaaaaaa/f62aaaaaaaaaaaaa/f66aaaaaaaaaaaaa/"
        "f6aaaaaaaaaaaaaa/f6eaaaaaaaaaaaaa/f72aaaaaaaaaaaaa/f76aaaaaaaaaaaaa/"
        "f7aaaaaaaaaaaaaa/f7eaaaaaaaaaaaaa/f82aaaaaaaaaaaaa/f86aaaaaaaaaaaaa/"
        "f8aaaaaaaaaaaaaa/f8eaaaaaaaaaaaaa/f92aaaaaaaaaaaaa/f96aaaaaaaaaaaaa/"
        "f9aaaaaaaaaaaaaa/f9eaaaaaaaaaaaaa/fa2aaaaaaaaaaaaa/fa6aaaaaaaaaaaaa/"
        "faaaaaaaaaaaaaaa/faeaaaaaaaaaaaaa/fb2aaaaaaaaaaaaa/fb6aaaaaaaaaaaaa/"
        "fbaaaaaaaaaaaaaa/fbeaaaaaaaaaaaaa/fc2aaaaaaaaaaaaa/fc6aaaaaaaaaaaaa/"
        "fcaaaaaaaaaaaaaa/fceaaaaaaaaaaaaa/fd2aaaaaaaaaaaaa/fd6aaaaaaaaaaaaa/"
        "fdaaaaaaaaaaaaaa/fdeaaaaaaaaaaaaa/fe2aaaaaaaaaaaaa/fe6aaaaaaaaaaaaa/"
        "feaaaaaaaaaaaaaa/feeaaaaaaaaaaaaa/ff2aaaaaaaaaaaaa/ff6aaaaaaaaaaaaa/"
        "ffaaaaaaaaaaaaaa/ffeaaaaaaaaaaaaa"
    );

    // Then the operation fails
    REQUIRE(!ok);

    // And the value returned by Get()/CStr() reflects the original, unchanged value
    REQUIRE(path.Get() == "/foo/bar/baz");
    REQUIRE(std::strcmp(path.CStr(), "/foo/bar/baz") == 0);
  }

  SECTION("M fail and leave value intact W Join is called with too-long value") {
    // Given a StoragePath value that currently holds /foo/bar/baz
    StoragePath path;
    REQUIRE(path.Set("/foo/bar/baz"));

    // When we call Append() with a value that exceeds MAX_STORAGE_PATH_SIZE
    const bool ok = path.Append(
        "/tmp/aaaaaaaaaaaaaaaa/aaeaaaaaaaaaaaaa/ab2aaaaaaaaaaaaa/ab6aaaaaaaaaaaaa/"
        "abaaaaaaaaaaaaaa/abeaaaaaaaaaaaaa/ac2aaaaaaaaaaaaa/ac6aaaaaaaaaaaaa/"
        "acaaaaaaaaaaaaaa/aceaaaaaaaaaaaaa/ad2aaaaaaaaaaaaa/ad6aaaaaaaaaaaaa/"
        "adaaaaaaaaaaaaaa/adeaaaaaaaaaaaaa/ae2aaaaaaaaaaaaa/ae6aaaaaaaaaaaaa/"
        "aeaaaaaaaaaaaaaa/aeeaaaaaaaaaaaaa/af2aaaaaaaaaaaaa/af6aaaaaaaaaaaaa/"
        "afaaaaaaaaaaaaaa/afeaaaaaaaaaaaaa/b02aaaaaaaaaaaaa/b06aaaaaaaaaaaaa/"
        "b0aaaaaaaaaaaaaa/b0eaaaaaaaaaaaaa/b12aaaaaaaaaaaaa/b16aaaaaaaaaaaaa/"
        "b1aaaaaaaaaaaaaa/b1eaaaaaaaaaaaaa/b22aaaaaaaaaaaaa/b26aaaaaaaaaaaaa/"
        "b2aaaaaaaaaaaaaa/b2eaaaaaaaaaaaaa/b32aaaaaaaaaaaaa/b36aaaaaaaaaaaaa/"
        "b3aaaaaaaaaaaaaa/b3eaaaaaaaaaaaaa/b42aaaaaaaaaaaaa/b46aaaaaaaaaaaaa/"
        "b4aaaaaaaaaaaaaa/b4eaaaaaaaaaaaaa/b52aaaaaaaaaaaaa/b56aaaaaaaaaaaaa/"
        "b5aaaaaaaaaaaaaa/b5eaaaaaaaaaaaaa/b62aaaaaaaaaaaaa/b66aaaaaaaaaaaaa/"
        "b6aaaaaaaaaaaaaa/b6eaaaaaaaaaaaaa/b72aaaaaaaaaaaaa/b76aaaaaaaaaaaaa/"
        "b7aaaaaaaaaaaaaa/b7eaaaaaaaaaaaaa/b82aaaaaaaaaaaaa/b86aaaaaaaaaaaaa/"
        "b8aaaaaaaaaaaaaa/b8eaaaaaaaaaaaaa/b92aaaaaaaaaaaaa/b96aaaaaaaaaaaaa/"
        "b9aaaaaaaaaaaaaa/b9eaaaaaaaaaaaaa/ba2aaaaaaaaaaaaa/ba6aaaaaaaaaaaaa/"
        "baaaaaaaaaaaaaaa/baeaaaaaaaaaaaaa/bb2aaaaaaaaaaaaa/bb6aaaaaaaaaaaaa/"
        "bbaaaaaaaaaaaaaa/bbeaaaaaaaaaaaaa/bc2aaaaaaaaaaaaa/bc6aaaaaaaaaaaaa/"
        "bcaaaaaaaaaaaaaa/bceaaaaaaaaaaaaa/bd2aaaaaaaaaaaaa/bd6aaaaaaaaaaaaa/"
        "bdaaaaaaaaaaaaaa/bdeaaaaaaaaaaaaa/be2aaaaaaaaaaaaa/be6aaaaaaaaaaaaa/"
        "beaaaaaaaaaaaaaa/beeaaaaaaaaaaaaa/bf2aaaaaaaaaaaaa/bf6aaaaaaaaaaaaa/"
        "bfaaaaaaaaaaaaaa/bfeaaaaaaaaaaaaa/c02aaaaaaaaaaaaa/c06aaaaaaaaaaaaa/"
        "c0aaaaaaaaaaaaaa/c0eaaaaaaaaaaaaa/c12aaaaaaaaaaaaa/c16aaaaaaaaaaaaa/"
        "c1aaaaaaaaaaaaaa/c1eaaaaaaaaaaaaa/c22aaaaaaaaaaaaa/c26aaaaaaaaaaaaa/"
        "c2aaaaaaaaaaaaaa/c2eaaaaaaaaaaaaa/c32aaaaaaaaaaaaa/c36aaaaaaaaaaaaa/"
        "c3aaaaaaaaaaaaaa/c3eaaaaaaaaaaaaa/c42aaaaaaaaaaaaa/c46aaaaaaaaaaaaa/"
        "c4aaaaaaaaaaaaaa/c4eaaaaaaaaaaaaa/c52aaaaaaaaaaaaa/c56aaaaaaaaaaaaa/"
        "c5aaaaaaaaaaaaaa/c5eaaaaaaaaaaaaa/c62aaaaaaaaaaaaa/c66aaaaaaaaaaaaa/"
        "c6aaaaaaaaaaaaaa/c6eaaaaaaaaaaaaa/c72aaaaaaaaaaaaa/c76aaaaaaaaaaaaa/"
        "c7aaaaaaaaaaaaaa/c7eaaaaaaaaaaaaa/c82aaaaaaaaaaaaa/c86aaaaaaaaaaaaa/"
        "c8aaaaaaaaaaaaaa/c8eaaaaaaaaaaaaa/c92aaaaaaaaaaaaa/c96aaaaaaaaaaaaa/"
        "c9aaaaaaaaaaaaaa/c9eaaaaaaaaaaaaa/ca2aaaaaaaaaaaaa/ca6aaaaaaaaaaaaa/"
        "caaaaaaaaaaaaaaa/caeaaaaaaaaaaaaa/cb2aaaaaaaaaaaaa/cb6aaaaaaaaaaaaa/"
        "cbaaaaaaaaaaaaaa/cbeaaaaaaaaaaaaa/cc2aaaaaaaaaaaaa/cc6aaaaaaaaaaaaa/"
        "ccaaaaaaaaaaaaaa/cceaaaaaaaaaaaaa/cd2aaaaaaaaaaaaa/cd6aaaaaaaaaaaaa/"
        "cdaaaaaaaaaaaaaa/cdeaaaaaaaaaaaaa/ce2aaaaaaaaaaaaa/ce6aaaaaaaaaaaaa/"
        "ceaaaaaaaaaaaaaa/ceeaaaaaaaaaaaaa/cf2aaaaaaaaaaaaa/cf6aaaaaaaaaaaaa/"
        "cfaaaaaaaaaaaaaa/cfeaaaaaaaaaaaaa/d02aaaaaaaaaaaaa/d06aaaaaaaaaaaaa/"
        "d0aaaaaaaaaaaaaa/d0eaaaaaaaaaaaaa/d12aaaaaaaaaaaaa/d16aaaaaaaaaaaaa/"
        "d1aaaaaaaaaaaaaa/d1eaaaaaaaaaaaaa/d22aaaaaaaaaaaaa/d26aaaaaaaaaaaaa/"
        "d2aaaaaaaaaaaaaa/d2eaaaaaaaaaaaaa/d32aaaaaaaaaaaaa/d36aaaaaaaaaaaaa/"
        "d3aaaaaaaaaaaaaa/d3eaaaaaaaaaaaaa/d42aaaaaaaaaaaaa/d46aaaaaaaaaaaaa/"
        "d4aaaaaaaaaaaaaa/d4eaaaaaaaaaaaaa/d52aaaaaaaaaaaaa/d56aaaaaaaaaaaaa/"
        "d5aaaaaaaaaaaaaa/d5eaaaaaaaaaaaaa/d62aaaaaaaaaaaaa/d66aaaaaaaaaaaaa/"
        "d6aaaaaaaaaaaaaa/d6eaaaaaaaaaaaaa/d72aaaaaaaaaaaaa/d76aaaaaaaaaaaaa/"
        "d7aaaaaaaaaaaaaa/d7eaaaaaaaaaaaaa/d82aaaaaaaaaaaaa/d86aaaaaaaaaaaaa/"
        "d8aaaaaaaaaaaaaa/d8eaaaaaaaaaaaaa/d92aaaaaaaaaaaaa/d96aaaaaaaaaaaaa/"
        "d9aaaaaaaaaaaaaa/d9eaaaaaaaaaaaaa/da2aaaaaaaaaaaaa/da6aaaaaaaaaaaaa/"
        "daaaaaaaaaaaaaaa/daeaaaaaaaaaaaaa/db2aaaaaaaaaaaaa/db6aaaaaaaaaaaaa/"
        "dbaaaaaaaaaaaaaa/dbeaaaaaaaaaaaaa/dc2aaaaaaaaaaaaa/dc6aaaaaaaaaaaaa/"
        "dcaaaaaaaaaaaaaa/dceaaaaaaaaaaaaa/dd2aaaaaaaaaaaaa/dd6aaaaaaaaaaaaa/"
        "ddaaaaaaaaaaaaaa/ddeaaaaaaaaaaaaa/de2aaaaaaaaaaaaa/de6aaaaaaaaaaaaa/"
        "deaaaaaaaaaaaaaa/deeaaaaaaaaaaaaa/df2aaaaaaaaaaaaa/df6aaaaaaaaaaaaa/"
        "dfaaaaaaaaaaaaaa/dfeaaaaaaaaaaaaa/e02aaaaaaaaaaaaa/e06aaaaaaaaaaaaa/"
        "e0aaaaaaaaaaaaaa/e0eaaaaaaaaaaaaa/e12aaaaaaaaaaaaa/e16aaaaaaaaaaaaa/"
        "e1aaaaaaaaaaaaaa/e1eaaaaaaaaaaaaa/e22aaaaaaaaaaaaa/e26aaaaaaaaaaaaa/"
        "e2aaaaaaaaaaaaaa/e2eaaaaaaaaaaaaa/e32aaaaaaaaaaaaa/e36aaaaaaaaaaaaa/"
        "e3aaaaaaaaaaaaaa/e3eaaaaaaaaaaaaa/e42aaaaaaaaaaaaa/e46aaaaaaaaaaaaa/"
        "e4aaaaaaaaaaaaaa/e4eaaaaaaaaaaaaa/e52aaaaaaaaaaaaa/e56aaaaaaaaaaaaa/"
        "e5aaaaaaaaaaaaaa/e5eaaaaaaaaaaaaa/e62aaaaaaaaaaaaa/e66aaaaaaaaaaaaa/"
        "e6aaaaaaaaaaaaaa/e6eaaaaaaaaaaaaa/e72aaaaaaaaaaaaa/e76aaaaaaaaaaaaa/"
        "e7aaaaaaaaaaaaaa/e7eaaaaaaaaaaaaa/e82aaaaaaaaaaaaa/e86aaaaaaaaaaaaa/"
        "e8aaaaaaaaaaaaaa/e8eaaaaaaaaaaaaa/e92aaaaaaaaaaaaa/e96aaaaaaaaaaaaa/"
        "e9aaaaaaaaaaaaaa/e9eaaaaaaaaaaaaa/ea2aaaaaaaaaaaaa/ea6aaaaaaaaaaaaa/"
        "eaaaaaaaaaaaaaaa/eaeaaaaaaaaaaaaa/eb2aaaaaaaaaaaaa/eb6aaaaaaaaaaaaa/"
        "ebaaaaaaaaaaaaaa/ebeaaaaaaaaaaaaa/ec2aaaaaaaaaaaaa/ec6aaaaaaaaaaaaa/"
        "ecaaaaaaaaaaaaaa/eceaaaaaaaaaaaaa/ed2aaaaaaaaaaaaa/ed6aaaaaaaaaaaaa/"
        "edaaaaaaaaaaaaaa/edeaaaaaaaaaaaaa/ee2aaaaaaaaaaaaa/ee6aaaaaaaaaaaaa/"
        "eeaaaaaaaaaaaaaa/eeeaaaaaaaaaaaaa/ef2aaaaaaaaaaaaa/ef6aaaaaaaaaaaaa/"
        "efaaaaaaaaaaaaaa/efeaaaaaaaaaaaaa/f02aaaaaaaaaaaaa/f06aaaaaaaaaaaaa/"
        "f0aaaaaaaaaaaaaa/f0eaaaaaaaaaaaaa/f12aaaaaaaaaaaaa/f16aaaaaaaaaaaaa/"
        "f1aaaaaaaaaaaaaa/f1eaaaaaaaaaaaaa/f22aaaaaaaaaaaaa/f26aaaaaaaaaaaaa/"
        "f2aaaaaaaaaaaaaa/f2eaaaaaaaaaaaaa/f32aaaaaaaaaaaaa/f36aaaaaaaaaaaaa/"
        "f3aaaaaaaaaaaaaa/f3eaaaaaaaaaaaaa/f42aaaaaaaaaaaaa/f46aaaaaaaaaaaaa/"
        "f4aaaaaaaaaaaaaa/f4eaaaaaaaaaaaaa/f52aaaaaaaaaaaaa/f56aaaaaaaaaaaaa/"
        "f5aaaaaaaaaaaaaa/f5eaaaaaaaaaaaaa/f62aaaaaaaaaaaaa/f66aaaaaaaaaaaaa/"
        "f6aaaaaaaaaaaaaa/f6eaaaaaaaaaaaaa/f72aaaaaaaaaaaaa/f76aaaaaaaaaaaaa/"
        "f7aaaaaaaaaaaaaa/f7eaaaaaaaaaaaaa/f82aaaaaaaaaaaaa/f86aaaaaaaaaaaaa/"
        "f8aaaaaaaaaaaaaa/f8eaaaaaaaaaaaaa/f92aaaaaaaaaaaaa/f96aaaaaaaaaaaaa/"
        "f9aaaaaaaaaaaaaa/f9eaaaaaaaaaaaaa/fa2aaaaaaaaaaaaa/fa6aaaaaaaaaaaaa/"
        "faaaaaaaaaaaaaaa/faeaaaaaaaaaaaaa/fb2aaaaaaaaaaaaa/fb6aaaaaaaaaaaaa/"
        "fbaaaaaaaaaaaaaa/fbeaaaaaaaaaaaaa/fc2aaaaaaaaaaaaa/fc6aaaaaaaaaaaaa/"
        "fcaaaaaaaaaaaaaa/fceaaaaaaaaaaaaa/fd2aaaaaaaaaaaaa/fd6aaaaaaaaaaaaa/"
        "fdaaaaaaaaaaaaaa/fdeaaaaaaaaaaaaa/fe2aaaaaaaaaaaaa/fe6aaaaaaaaaaaaa/"
        "feaaaaaaaaaaaaaa/feeaaaaaaaaaaaaa/ff2aaaaaaaaaaaaa/ff6aaaaaaaaaaaaa/"
        "ffaaaaaaaaaaaaaa/ffeaaaaaaaaaaaaa"
    );

    // Then the operation fails
    REQUIRE(!ok);

    // And the value returned by Get()/CStr() reflects the original, unchanged value
    REQUIRE(path.Get() == "/foo/bar/baz");
    REQUIRE(std::strcmp(path.CStr(), "/foo/bar/baz") == 0);
  }

  SECTION("M remove trailing path component W Pop is called") {
    // Given a StoragePath value that currently holds /foo/bar/baz
    StoragePath path;
    REQUIRE(path.Set("/foo/bar/baz"));

    // When we call Pop()
    path.Pop();

    // Then the value is now /foo/bar
    REQUIRE(path.Get() == "/foo/bar");
    REQUIRE(std::strcmp(path.CStr(), "/foo/bar") == 0);
  }

  SECTION("M handle trailing slash W Pop is called") {
    // Given a StoragePath value that currently holds /foo/bar/baz/
    StoragePath path;
    REQUIRE(path.Set("/foo/bar/baz/"));

    // When we call Pop()
    path.Pop();

    // Then the value is now /foo/bar
    REQUIRE(path.Get() == "/foo/bar");
    REQUIRE(std::strcmp(path.CStr(), "/foo/bar") == 0);
  }

#ifdef _WIN32
  SECTION("M treat backslash as path delimiter W Pop is called") {
    // On Windows, a trailing backslash is recognized as a delimiter
    StoragePath path;
    REQUIRE(path.Set("C:\\Temp\\foo"));
    path.Pop();
    REQUIRE(path.Get() == "C:\\Temp");
  }
#else
  SECTION("M treat backlash as ordinary filename character W Pop is called") {
    // On all other platforms, a backslash has no special meaning
    StoragePath path;
    REQUIRE(path.Set("/tmp/foo\\bar"));
    path.Pop();
    REQUIRE(path.Get() == "/tmp");
  }
#endif

#ifdef _WIN32
  SECTION("M recognize trailing backslash W Pop is called") {
    // On Windows, a trailing backslash is recognized as a delimiter
    StoragePath path;
    REQUIRE(path.Set("c:/Temp/foo\\"));
    path.Pop();
    REQUIRE(path.Get() == "c:/Temp");
  }
#else
  SECTION("M ignore trailing backlash W Pop is called") {
    // On all other platforms, a backslash has no special meaning
    StoragePath path;
    REQUIRE(path.Set("/tmp/foo\\"));
    path.Pop();
    REQUIRE(path.Get() == "/tmp");
  }
#endif

  SECTION("M leave path unmodified W Pop is called on root path") {
    // Given a StoragePath value that currently holds /
    StoragePath path;
    REQUIRE(path.Set("/"));

    // When we call Pop()
    path.Pop();

    // Then the value is still /
    REQUIRE(path.Get() == "/");
    REQUIRE(std::strcmp(path.CStr(), "/") == 0);
  }

#ifdef _WIN32
  SECTION("M leave path unmodified W Pop is called on root path with drive letter") {
    // Given a StoragePath value that currently holds "C:\"
    StoragePath path;
    REQUIRE(path.Set("C:\\"));

    // When we call Pop()
    path.Pop();

    // Then the value is still "C:\"
    REQUIRE(path.Get() == "C:\\");
    REQUIRE(std::strcmp(path.CStr(), "C:\\") == 0);
  }
#endif

  SECTION("M leave path unmodified W Pop is called on empty path") {
    StoragePath path;
    path.Pop();
    REQUIRE(path.Get() == "");
    REQUIRE(std::strcmp(path.CStr(), "") == 0);
  }

  SECTION("M leave path unmodified W Pop is called on current-dir dot") {
    StoragePath path;
    REQUIRE(path.Set("."));
    path.Pop();
    REQUIRE(path.Get() == ".");
    REQUIRE(std::strcmp(path.CStr(), ".") == 0);
  }

  SECTION("M leave path unmodified W Pop is called on top-level dir in relative path") {
    StoragePath path;
    REQUIRE(path.Set("my-dir"));
    path.Pop();
    REQUIRE(path.Get() == "my-dir");
    REQUIRE(std::strcmp(path.CStr(), "my-dir") == 0);
  }

  SECTION(
      "M leave path unmodified W Pop is called on top-level dir with trailing slash"
  ) {
    StoragePath path;
    REQUIRE(path.Set("my-dir/"));
    path.Pop();
    REQUIRE(path.Get() == "my-dir/");
    REQUIRE(std::strcmp(path.CStr(), "my-dir/") == 0);
  }
}

TEST_CASE("PlatformPath", "[unit][storage]") {
#ifdef _WIN32
  SECTION("M succeed and store UTF-16 value W Encode is called with valid string") {
    // Given a StoragePath that holds C:\Temp\Foo\Bar\some-dir
    StoragePath path;
    REQUIRE(path.Join("C:\\Temp\\Foo\\Bar", "some-dir"));

    // When we encode that value for use in calls to Win32 system APIs
    PlatformPath platform_path;
    const bool ok = platform_path.Encode(path.CStr());

    // Then the operation succeeds
    REQUIRE(ok);

    // And the value returned by Get() is equivalent to our original input value, but
    // encoded as a UTF-16 wide string
    REQUIRE(std::wcscmp(platform_path.Get(), L"C:\\Temp\\Foo\\Bar\\some-dir") == 0);
  }

  SECTION("M convert non-ASCII characters to UTF-16") {
    // Given a StoragePath that holds C:\Temp\Foo\Bar\sömë-dïr
    StoragePath path;
    REQUIRE(path.Join("C:\\Temp\\Foo\\Bar", "sömë-dïr"));

    // When we encode that value for use in calls to Win32 system APIs
    PlatformPath platform_path;
    const bool ok = platform_path.Encode(path.CStr());

    // Then the operation succeeds
    REQUIRE(ok);

    // Then the value returned by Get() is equivalent to our original input value, but
    // encoded as a UTF-16 wide string
    REQUIRE(std::wcscmp(platform_path.Get(), L"C:\\Temp\\Foo\\Bar\\sömë-dïr") == 0);
  }

  SECTION("M fail to encode W Encode is called with invalid UTF-8 string") {
    // Given a StoragePath that's initialized with a lone continuation byte, which does
    // not constitute a valid UTF-8 string
    StoragePath path;
    REQUIRE(path.Set("\x80"));

    // When we encode that value for use in calls to Win32 system APIs
    PlatformPath platform_path;
    const bool ok = platform_path.Encode(path.CStr());

    // Then the operation fails
    REQUIRE(!ok);
  }
#else
  SECTION("M transparently provide access to original UTF-8 value W Encode is called") {
    // Given a StoragePath that holds /tmp/foo/bar/some-dir
    StoragePath path;
    REQUIRE(path.Join("/tmp/foo/bar", "some-dir"));

    // When we "encode" that value for use in calls to POSIX system APIs
    PlatformPath platform_path;
    const bool ok = platform_path.Encode(path.CStr());

    // Then the operation succeeds
    REQUIRE(ok);

    // Then the value returned by Get() is equivalent to our original UTF-8 input value
    REQUIRE(std::strcmp(platform_path.Get(), "/tmp/foo/bar/some-dir") == 0);
  }

  SECTION("M relay non-ASCII characters as-is") {
    // Given a StoragePath that holds /tmp/foo/bar/sömë-dïr
    StoragePath path;
    REQUIRE(path.Join("/tmp/foo/bar", "sömë-dïr"));

    // When we "encode" that value for use in calls to POSIX system APIs
    PlatformPath platform_path;
    const bool ok = platform_path.Encode(path.CStr());

    // Then the operation succeeds
    REQUIRE(ok);

    // Then the value returned by Get() is equivalent to our original UTF-8 input value
    REQUIRE(std::strcmp(platform_path.Get(), "/tmp/foo/bar/sömë-dïr") == 0);
  }
#endif
}
