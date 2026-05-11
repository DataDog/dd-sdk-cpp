// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

struct MockBinaryFile {
  std::vector<uint8_t> bytes;

  MockBinaryFile& UInt64(uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
    return *this;
  }

  MockBinaryFile& UInt8(uint8_t value) {
    bytes.push_back(value);
    return *this;
  }

  MockBinaryFile& String(std::string_view value) {
    UInt64(value.size());
    for (char c : value) {
      bytes.push_back(static_cast<uint8_t>(c));
    }
    return *this;
  }

  MockBinaryFile& Bytes(std::initializer_list<uint8_t> value) {
    bytes.insert(bytes.end(), value.begin(), value.end());
    return *this;
  }

  // Parses a 36-char canonical UUID string and appends the 16 raw bytes.
  MockBinaryFile& UUID(std::string_view s) {
    auto nibble = [](char c) -> uint8_t {
      if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
      if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(c - 'a' + 10); }
      return static_cast<uint8_t>(c - 'A' + 10);
    };
    auto byte_at = [&](size_t hi, size_t lo) -> uint8_t {
      return static_cast<uint8_t>((nibble(s[hi]) << 4) | nibble(s[lo]));
    };
    // UUID format: 8-4-4-4-12, hyphens at positions 8, 13, 18, 23
    const size_t pos[] = {0,2,4,6, 9,11, 14,16, 19,21, 24,26,28,30,32,34};
    for (size_t p : pos) {
      bytes.push_back(byte_at(p, p + 1));
    }
    return *this;
  }

  std::string_view Get() const {
    const char* data = reinterpret_cast<const char*>(bytes.data());
    return std::string_view{data, bytes.size()};
  }
};
