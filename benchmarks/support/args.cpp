#include "support/args.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <iostream>

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

ParsedArg ParseArg(char* arg) {
  // If arg is not a string formatted '--foo', it's invalid
  if (!arg) {
    return ParsedArg{nullptr, nullptr};
  }
  const size_t n = std::strlen(arg);
  if (n < 3 || arg[0] != '-' || arg[1] != '-' || arg[2] == '-') {
    return ParsedArg{nullptr, nullptr};
  }

  // Take everything after '--' as the name, then check for '='
  char* name = arg + 2;
  char* equals_ptr = std::strchr(name, '=');

  // If there's no '=', assume an implicit value of '1'
  if (equals_ptr == nullptr) {
    return ParsedArg{name, "1"};
  }

  // Otherwise, clobber the equals sign to null-terminate the name, then take everything
  // after the equals sign as the value
  *equals_ptr = '\0';
  const char* value = equals_ptr + 1;
  return ParsedArg{name, value};
}

void PrintBenchmarkUsage(const Benchmark& b, const char* argv_0) {
  std::cout << b.name << ": " << b.description << "\n";
  std::cout << "Usage:\n";
  std::cout << "  " << argv_0 << " " << b.name << "[";
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

std::array<BenchmarkParamValue, MAX_ARGS> ParseBenchmarkArgs(
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
    auto arg = ParseArg(argv[i]);
    if (!arg.name || !arg.value) {
      std::cerr << "invalid argument format: " << argv[i] << "\n";
      std::exit(1);  // NOLINT(concurrency-mt-unsafe)
    }

    // Find the BenchmarkParam identified by this argument
    const BenchmarkParam* const param_iter =
        std::find_if(params.begin(), params.end(), [&](const BenchmarkParam& p) {
          return p.name != nullptr && std::strcmp(p.name, arg.name) == 0;
        });
    if (param_iter == params.end()) {
      std::cerr << "unrecognized argument: " << arg.name << "\n";
      std::exit(1);  // NOLINT(concurrency-mt-unsafe)
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
          std::exit(1);  // NOLINT(concurrency-mt-unsafe)
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
          std::exit(1);  // NOLINT(concurrency-mt-unsafe)
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
