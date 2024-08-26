#include "test.h"

#include <datadog/core.h>

using datadog::core::DatadogCore;
using datadog::core::DatadogFeature;
using datadog::core::FeatureId;

namespace {

class MockFeature : public DatadogFeature {
 public:
  static constexpr const FeatureId kFeatureId =
      datadog::core::four_cc('M', 'O', 'C', 'K');
};

TEST_CASE("M return null for unregistered feature W GetFeature", "[core]") {
  // Given
  DatadogCore core;

  // When
  auto feature = core.GetFeature<MockFeature>();

  // Then
  REQUIRE(!feature.has_value());
}

TEST_CASE("M return registered feature W register_feature", "[core]") {
  // Given
  DatadogCore core;
  core.RegisterFeature<MockFeature>();

  // When
  auto feature = core.GetFeature<MockFeature>();

  // Then
  REQUIRE(feature.has_value());
  REQUIRE(feature.value() != nullptr);
}

}  // namespace
