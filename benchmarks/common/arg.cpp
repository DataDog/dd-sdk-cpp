// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "common/arg.hpp"

#include <cstddef>
#include <cstring>

Arg ReadArg(char* arg) {
  // If arg is not a string formatted '--foo', it's invalid
  if (!arg) {
    return Arg{nullptr, nullptr};
  }
  const size_t n = std::strlen(arg);
  if (n < 3 || arg[0] != '-' || arg[1] != '-' || arg[2] == '-') {
    return Arg{nullptr, nullptr};
  }

  // Take everything after '--' as the name, then check for '='
  char* name = arg + 2;
  char* equals_ptr = std::strchr(name, '=');

  // If there's no '=', assume an implicit value of '1'
  if (equals_ptr == nullptr) {
    return Arg{name, "1"};
  }

  // Otherwise, clobber the equals sign to null-terminate the name, then take everything
  // after the equals sign as the value
  *equals_ptr = '\0';
  const char* value = equals_ptr + 1;
  return Arg{name, value};
}
