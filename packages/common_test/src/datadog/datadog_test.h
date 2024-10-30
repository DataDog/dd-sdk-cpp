// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <algorithm>
#include <random>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace datadog::test {

inline std::string GenerateRandomString(size_t length = 20) {
  using namespace std::literals::string_view_literals;

  constexpr auto kCharacters =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"sv;

  static std::random_device random_device;
  static std::mt19937 generator{random_device()};

  std::uniform_int_distribution<> distribution(0, kCharacters.size() - 1);

  std::string random_string(length, 0);
  std::generate_n(random_string.begin(), length,
                  [&] { return kCharacters[distribution(generator)]; });

  return random_string;
}

}  // namespace datadog::test
