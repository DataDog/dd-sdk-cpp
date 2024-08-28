#include "test.h"

#include <datadog/core.h>
#include <datadog/internal/utils.h>

using datadog::core::DatadogCore;
using datadog::core::DatadogFeature;
using datadog::core::FeatureId;

namespace {

class MockFeature : public DatadogFeature {
 public:
  explicit MockFeature(const std::shared_ptr<DatadogCore>& core) {}

  static constexpr FeatureId feature_id =
      datadog::core::internal::four_cc('M', 'O', 'C', 'K');

 private:
  MockFeature() = delete;
};

TEST_CASE("M return proper value W four_cc", "[core]") {
  // Given
  auto value = datadog::core::internal::four_cc('A', 'B', 'C', 'D');

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
  std::shared_ptr<DatadogCore> core = DatadogCore::Create();
  core->RegisterFeature<MockFeature>();

  // When
  auto feature = core->GetFeature<MockFeature>();

  // Then
  REQUIRE(feature != nullptr);
}

}  // namespace
