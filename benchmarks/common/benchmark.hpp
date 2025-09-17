// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cinttypes>
#include <cstddef>

/**
 * Hardcoded limit on number of command-line options that can be supplied for a
 * benchmark (or globally).
 */
static const size_t MAX_ARGS = 16;

/**
 * Type of command-line argument value.
 */
enum class BenchmarkParamType : uint8_t { Int, Double, String };

/**
 * Parsed command-line argument.
 */
union BenchmarkParamValue {
  int32_t i;
  double d;
  const char* s;
};

/**
 * Declaration of a parameter value accepted by a benchmark.
 */
struct BenchmarkParam {
  const char* name;
  BenchmarkParamType type;
  BenchmarkParamValue default_value;
  const char* description;

  static BenchmarkParam Int(
      const char* name, int32_t default_value, const char* description
  );
  static BenchmarkParam Double(
      const char* name, double default_value, const char* description
  );
  static BenchmarkParam String(
      const char* name, const char* default_value, const char* description
  );
};

/**
 * Declaration of a benchmark that can be invoked by name.
 */
struct Benchmark {
  const char* name;
  const char* description;
  std::array<BenchmarkParam, MAX_ARGS> params;
};

/**
 * Given a struct describing a specific Benchmark, prints usage information to stdout.
 */
void PrintBenchmarkUsage(const Benchmark& b, const char* argv_0);

/**
 * Given argc and argc values pointing the the command-line arguments appearing after a
 * benchmark name, parses parameter values according to the specified parameter
 * declarations.
 */
std::array<BenchmarkParamValue, MAX_ARGS> ParseBenchmarkParams(
    const std::array<BenchmarkParam, MAX_ARGS>& params, int argc, char* argv[]
);
