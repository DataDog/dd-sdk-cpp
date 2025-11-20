// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <istream>
#include <string_view>
#include <utility>

static const size_t BUFFER_SIZE = 1024;

struct Buffer {
  std::array<char, BUFFER_SIZE> bytes{};

  std::pair<std::string_view, bool> GetLine(std::istream& in);
  std::string_view Write(std::string_view s);
  std::string_view Writef(const char* fmt, ...);
};
