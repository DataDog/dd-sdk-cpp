#pragma once

#include <array>
#include <cinttypes>

static const size_t MAX_ARGS = 16;

enum class BenchmarkParamType : uint8_t { Int, Double, String };

union BenchmarkParamValue {
  int32_t i;
  double d;
  const char* s;
};

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

struct Benchmark {
  const char* name;
  const char* description;
  std::array<BenchmarkParam, MAX_ARGS> params;
};

struct ParsedArg {
  const char* name;
  const char* value;
};

ParsedArg ParseArg(char* arg);

void PrintBenchmarkUsage(const Benchmark& b, const char* argv_0);

std::array<BenchmarkParamValue, MAX_ARGS> ParseBenchmarkArgs(
    const std::array<BenchmarkParam, MAX_ARGS>& params, int argc, char* argv[]
);
