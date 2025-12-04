// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "repl/command.hpp"

#include <charconv>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)

namespace {

size_t get_named_arg_delim_pos(std::string_view s) {
  // If the input token represents a positional arg, it has no named-argument delimiter
  const auto npos = std::string_view::npos;

  // Empty token is treated as positional
  if (s.empty()) {
    return npos;
  }

  // A quoted token is treated as positional
  if (s[0] == '"') {
    return npos;
  }

  // A token with no colon is treated as positional
  const size_t first_colon_pos = s.find(':');
  if (first_colon_pos == std::string_view::npos) {
    return npos;
  }

  // Allow ':/' in positional args for URLs etc.
  const size_t slash_pos = s.find('/', first_colon_pos + 1);
  const bool is_probably_url = slash_pos == first_colon_pos + 1;
  if (is_probably_url) {
    return npos;
  }

  // If we have a colon that isn't immediately followed by a slash, this token is a
  // named argument that should be split on the first colon
  return first_colon_pos;
}

}  // namespace

std::string_view Unquote(std::string_view s) {
  if (s.size() > 1 && s.front() == '"' && s.back() == '"') {
    return std::string_view{s.data() + 1, s.size() - 2};
  }
  return s;
}

bool NamedValueList::Has(std::string_view name) const {
  for (size_t i = 0; i < n; i++) {
    if (values[i].name == name) {
      return true;
    }
  }
  return false;
}

std::string_view NamedValueList::Get(std::string_view name) const {
  for (size_t i = 0; i < n; i++) {
    if (values[i].name == name) {
      return values[i].value;
    }
  }
  return "";
}

int64_t NamedValueList::GetInt(std::string_view name) const {
  int64_t value{0};
  std::string_view str_value = Get(name);
  std::from_chars(str_value.begin(), str_value.end(), value);
  return value;
}

bool NamedValueList::GetFlag(std::string_view name) const {
  std::string_view str_value = Get(name);
  return str_value == "1" || str_value == "true" || str_value == "t" ||
         str_value == "yes" || str_value == "on";
}

std::string_view CommandInput::operator[](size_t i) const {
  if (i < n) {
    return tokens[i];
  }
  return "";
}

std::string_view CommandInput::Peek() const { return operator[](0); }

CommandInput CommandInput::Shift(size_t skip_count) const {
  if (skip_count > n) {  // NOLINT
    skip_count = n;
  }
  CommandInput tail{};
  tail.n = n - skip_count;
  for (size_t i = 0; i < tail.n; i++) {
    tail.tokens[i] = tokens[i + skip_count];
  }
  return tail;
}

CommandInput CommandInput::Positional() const {
  CommandInput positional{};
  for (size_t i = 0; i < n; i++) {
    std::string_view token = operator[](i);
    if (get_named_arg_delim_pos(token) == std::string_view::npos) {
      positional.tokens[positional.n++] = token;
    }
  }
  return positional;
}

NamedValueList CommandInput::Named() const {
  NamedValueList named{};
  for (size_t i = 0; i < n; i++) {
    std::string_view token = operator[](i);
    const size_t delim_pos = get_named_arg_delim_pos(token);
    if (delim_pos != std::string_view::npos) {
      const char* data = token.data();
      std::string_view name{data, delim_pos};
      std::string_view value{data + delim_pos + 1, token.size() - delim_pos - 1};
      const size_t dst = named.n++;
      named.values[dst].name = name;
      named.values[dst].value = value;
    }
  }
  return named;
}

CommandInput CommandInput::Parse(std::string_view line) {
  CommandInput result{};
  size_t start = line.find_first_not_of(" \t");
  while (start != std::string_view::npos) {
    size_t end{};
    // Check if we're starting inside a quoted string
    size_t quote_pos = line.find('"', start);
    size_t space_pos = line.find_first_of(" \t", start);

    // If there's a quote before any space (or no space), we need to find the closing
    // quote
    if (quote_pos != std::string_view::npos &&
        (space_pos == std::string_view::npos || quote_pos < space_pos)) {
      // Find the closing quote after the opening quote
      size_t closing_quote = line.find('"', quote_pos + 1);
      if (closing_quote != std::string_view::npos) {
        // Token extends from start to after the closing quote
        end = line.find_first_of(" \t", closing_quote + 1);
      } else {
        // No closing quote, treat rest of line as token
        end = std::string_view::npos;
      }
    } else {
      // No quote involved, just find the next whitespace
      end = space_pos;
    }

    std::string_view word = line.substr(start, end - start);
    if (result.n < result.tokens.max_size()) {
      result.tokens[result.n++] = word;
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = line.find_first_not_of(" \t", end);
  }
  return result;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
