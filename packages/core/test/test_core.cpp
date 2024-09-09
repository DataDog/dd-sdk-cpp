#include "test.h"

#include <datadog/core.h>

namespace {

using datadog::core::DatadogCore;
using datadog::core::DatadogFeature;
using datadog::core::FeatureId;
using datadog::core::IDatadogCore;

class MockFeature : public DatadogFeature {
 public:
  explicit MockFeature(const std::weak_ptr<IDatadogCore>& core) : core_(core) {}

  static constexpr FeatureId feature_id =
      datadog::core::four_cc('M', 'O', 'C', 'K');

  std::weak_ptr<IDatadogCore> core_;
};

TEST_CASE("M return proper value W four_cc", "[core]") {
  // Given
  auto value = datadog::core::four_cc('A', 'B', 'C', 'D');

  REQUIRE(FeatureId{0x44434241} == value);
}

TEST_CASE("M return null for unregistered feature W GetFeature", "[core]") {
  // Given
  auto core = DatadogCore::Create();

  // When
  auto feature = core->GetFeature<MockFeature>();

  // Then
  REQUIRE(feature == nullptr);
}

TEST_CASE("M return registered feature W register_feature", "[core]") {
  // Given
  auto core = DatadogCore::Create();
  core->RegisterFeature<MockFeature>();

  // When
  auto feature = core->GetFeature<MockFeature>();

  // Then
  REQUIRE(feature != nullptr);
}

TEST_CASE("M not overwrite registered feature W second register", "[core]") {
  // Given
  auto core = DatadogCore::Create();
  core->RegisterFeature<MockFeature>();
  auto feature = core->GetFeature<MockFeature>();

  // When
  core->RegisterFeature<MockFeature>();
  auto second_feature = core->GetFeature<MockFeature>();

  REQUIRE(feature == second_feature);
}

}  // namespace
