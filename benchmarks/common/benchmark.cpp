#include "common/benchmark.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <iostream>

#include "common/arg.hpp"
#include "common/exit.hpp"

BenchmarkParam BenchmarkParam::Int(
    const char* name, int32_t default_value, const char* description
) {
  BenchmarkParam p{name, BenchmarkParamType::Int, {0}, description};
  p.default_value.i = default_value;
  return p;
}

BenchmarkParam BenchmarkParam::Double(
    const char* name, double default_value, const char* description
) {
  BenchmarkParam p{name, BenchmarkParamType::Double, {0}, description};
  p.default_value.d = default_value;
  return p;
}

BenchmarkParam BenchmarkParam::String(
    const char* name, const char* default_value, const char* description
) {
  BenchmarkParam p{name, BenchmarkParamType::String, {0}, description};
  p.default_value.s = default_value;
  return p;
}

void PrintBenchmarkUsage(const Benchmark& b, const char* argv_0) {
  std::cout << b.name << ": " << b.description << "\n";
  std::cout << "Usage:\n";
  std::cout << "  " << argv_0 << " [global-opts] " << b.name << "[";
  for (const BenchmarkParam& p : b.params) {
    if (p.name) {
      std::cout << " --" << p.name << " ";
      switch (p.type) {
        case BenchmarkParamType::Int:
          std::cout << p.default_value.i;
          break;
        case BenchmarkParamType::Double:
          std::cout << p.default_value.d;
          break;
        case BenchmarkParamType::String:
          std::cout << p.default_value.s;
          break;
      }
    }
  }
  std::cout << "]\n";
  std::cout << "Parameters:\n";
  for (const BenchmarkParam& p : b.params) {
    if (p.name) {
      std::cout << "  --" << p.name << ": " << p.description << "\n";
    }
  }
}

std::array<BenchmarkParamValue, MAX_ARGS> ParseBenchmarkParams(
    const std::array<BenchmarkParam, MAX_ARGS>& params, int argc, char* argv[]
) {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)

  // Populate the array of arg values with defaults
  std::array<BenchmarkParamValue, MAX_ARGS> result{};
  for (size_t i = 0; i < MAX_ARGS; i++) {
    switch (params[i].type) {
      case BenchmarkParamType::Int:
        result[i].i = params[i].default_value.i;
        break;
      case BenchmarkParamType::Double:
        result[i].d = params[i].default_value.d;
        break;
      case BenchmarkParamType::String:
        result[i].s = params[i].default_value.s;
        break;
    }
  }

  for (int i = 0; i < argc; i++) {
    // Parse the argument as '--foo' or '--foo=bar'
    Arg arg = ReadArg(argv[i]);
    if (!arg.name || !arg.value) {
      std::cerr << "invalid argument format: " << argv[i] << "\n";
      Exit(1);
    }

    // Find the BenchmarkParam identified by this argument
    const BenchmarkParam* const param_iter =
        std::find_if(params.begin(), params.end(), [&](const BenchmarkParam& p) {
          return p.name != nullptr && std::strcmp(p.name, arg.name) == 0;
        });
    if (param_iter == params.end()) {
      std::cerr << "unrecognized argument: " << arg.name << "\n";
      Exit(1);
    }

    // Store the user-supplied value in our array, overwriting the default
    const size_t param_index = std::distance(params.begin(), param_iter);
    switch (param_iter->type) {
      case BenchmarkParamType::Int: {
        // Parse the value as an integer
        int32_t v = 0;
        const size_t len = std::strlen(arg.value);
        auto res = std::from_chars(arg.value, arg.value + len, v);
        if (res.ec != std::errc{}) {
          std::cerr << "invalid integer '" << arg.value << "' for arg: " << arg.name
                    << "\n";
          Exit(1);
        }
        result[param_index].i = v;
      } break;
      case BenchmarkParamType::Double: {
        // Parse the value as a double
        try {
          double v = std::stod(arg.value);
          result[param_index].d = v;
        } catch (const std::exception&) {
          std::cerr << "invalid double '" << arg.value << "' for arg: " << arg.name
                    << "\n";
          Exit(1);
        }
      } break;
      case BenchmarkParamType::String: {
        result[param_index].s = arg.value;
        // Store the string directly
      }
    }
  }
  return result;

  // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
}
