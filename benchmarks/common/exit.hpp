#pragma once

#include <cstdlib>

inline void Exit(int status_code) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  std::exit(status_code);
}
