#include "test.h"

#include <datadog/core.h>

using namespace datadog::core;

TEST_CASE("dummy_function returns 8", "[core]") {
  Core core;

  REQUIRE(core.dummy_function() == 8);
}
