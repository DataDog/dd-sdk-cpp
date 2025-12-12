// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cinttypes>
#include <string_view>

static const size_t MAX_INPUT_TOKENS = 64;

std::string_view Unquote(std::string_view s);

struct CommandResult {
  bool ok;
  std::string_view message;

  static CommandResult OK(std::string_view message) {
    return CommandResult{true, message};
  }

  static CommandResult Error(std::string_view message) {
    return CommandResult{false, message};
  }
};

struct NamedValue {
  std::string_view name;
  std::string_view value;
};

struct NamedValueList {
  std::array<NamedValue, MAX_INPUT_TOKENS> values;
  size_t n{0};

  bool Has(std::string_view name) const;
  std::string_view Get(std::string_view name) const;
  int64_t GetInt(std::string_view name) const;
  bool GetFlag(std::string_view name) const;
};

struct CommandInput {
  std::array<std::string_view, MAX_INPUT_TOKENS> tokens;
  size_t n{0};

  std::string_view operator[](size_t i) const;
  std::string_view Peek() const;
  CommandInput Shift(size_t skip_count = 1) const;
  CommandInput Positional() const;
  NamedValueList Named() const;

  static CommandInput Parse(std::string_view line);
};
