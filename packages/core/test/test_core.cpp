#include "test.h"

#include <datadog/core.h>
#include <datadog/internal/utils.h>

namespace {

using datadog::core::DatadogConfiguration;
using datadog::core::DatadogCore;
using datadog::core::DatadogFeature;
using datadog::core::FeatureId;
using datadog::core::IDatadogCore;
using datadog::core::TrackingConsent;

class MockFeature : public DatadogFeature {
 public:
  explicit MockFeature(const std::weak_ptr<IDatadogCore>& core) : core_(core) {}

  std::string_view GetName() const override { return "MockFeature"; }

  static constexpr FeatureId feature_id =
      datadog::core::internal::CreateFourCC('M', 'O', 'C', 'K');

  std::weak_ptr<IDatadogCore> core_;
};

TEST_CASE("M return proper value W four_cc", "[core]") {
  // Given
  auto value = datadog::core::internal::CreateFourCC('A', 'B', 'C', 'D');

  REQUIRE(FeatureId{0x44434241} == value);
}

TEST_CASE("M return null for unregistered feature W GetFeature", "[core]") {
  // Given
  DatadogConfiguration config{TrackingConsent::Granted};
  auto core = DatadogCore::Create(config);

  // When
  auto feature = core->GetFeature<MockFeature>();

  // Then
  REQUIRE(feature == nullptr);
}

TEST_CASE("M return registered feature W register_feature", "[core]") {
  // Given
  DatadogConfiguration config{TrackingConsent::Granted};
  auto core = DatadogCore::Create(config);
  core->RegisterFeature<MockFeature>();

  // When
  auto feature = core->GetFeature<MockFeature>();

  // Then
  REQUIRE(feature != nullptr);
}

TEST_CASE("M not overwrite registered feature W second register", "[core]") {
  // Given
  DatadogConfiguration config{TrackingConsent::Granted};
  auto core = DatadogCore::Create(config);
  core->RegisterFeature<MockFeature>();
  auto feature = core->GetFeature<MockFeature>();

  // When
  core->RegisterFeature<MockFeature>();
  auto second_feature = core->GetFeature<MockFeature>();

  REQUIRE(feature == second_feature);
}

}  // namespace
