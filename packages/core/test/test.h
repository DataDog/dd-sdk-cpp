// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <random>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace datadog::test {

inline std::string GenerateRandomString(size_t length) {
  static constexpr std::string_view kCharacters =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

  static std::random_device random_device;
  static std::mt19937 generator(random_device());

  std::uniform_int_distribution<> distribution(0, kCharacters.size() - 1);

  std::string random_string(length, 0);
  std::generate_n(random_string.begin(), length,
                  [&] { return kCharacters[distribution(generator)]; });

  return random_string;
}

}  // namespace datadog::test
