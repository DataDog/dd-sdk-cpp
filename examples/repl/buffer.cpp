// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "repl/buffer.hpp"

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iostream>

std::pair<std::string_view, bool> Buffer::GetLine(std::istream& in) {
  in.getline(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (in.fail() && !in.eof()) {
    std::cerr << "FATAL: GetLine() read failed; buffer size may be insufficient\n";
    std::abort();
  }
  return {std::string_view{bytes.data()}, in.eof()};
}

std::string_view Buffer::Write(std::string_view s) {
  const size_t num_bytes = s.size() + 1;
  if (num_bytes > bytes.size()) {
    std::cerr << "FATAL: Insufficient buffer size on Write()\n";
    std::abort();
  }
  std::memcpy(bytes.data(), s.data(), s.size());
  bytes[s.size()] = '\0';  // NOLINT
  return std::string_view{bytes.data(), s.size()};
}

std::string_view Buffer::Writef(const char* fmt, ...) {
  // Call vsnprintf, passing our variadic args
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-init-variables)
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(bytes.data(), bytes.size(), fmt, args);
  va_end(args);

  // vsnprintf returns negative if it couldn't write anything
  if (n < 0) {
    std::cerr << "FATAL: Writef() formatting error\n";
    std::abort();
  }

  // vsnprintf null-terminates the formatted string and returns the number of bytes
  // written, excluding the null terminator: so len >= bytes.size() indicates truncation
  const size_t len = static_cast<size_t>(n);
  if (len >= bytes.size()) {
    std::cerr << "FATAL: Insufficient buffer size on Writef()\n";
    std::abort();
  }

  return std::string_view{bytes.data(), len};
}
