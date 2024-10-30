// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include <datadog/core.h>
#include <datadog/internal/utils.h>

#include <trompeloeil.hpp>

#include "datadog/datadog_test.h"
#include "storage/mock_datadog_file_system.h"

namespace {

using datadog::core::CoreMessage;
using datadog::core::DatadogConfiguration;
using datadog::core::DatadogCore;
using datadog::core::DatadogFeature;
using datadog::core::FeatureId;
using datadog::core::IDatadogCore;
using datadog::core::TrackingConsent;
using datadog::core::storage::mocks::MockDatadogFile;
using datadog::core::storage::mocks::MockDatadogFileSystem;
using datadog::test::GenerateRandomString;

using trompeloeil::_;
using trompeloeil::ne;

class MockFeature : public DatadogFeature {
 public:
  explicit MockFeature(const std::weak_ptr<IDatadogCore>& core) : core_(core) {}

  std::string_view GetName() const override { return "mock"; }

  static constexpr FeatureId feature_id =
      datadog::core::internal::CreateFourCC('M', 'O', 'C', 'K');

  std::weak_ptr<IDatadogCore> core_;
};

TEST_CASE("M return proper value W four_cc", "[core]") {
  // Given
  auto value = datadog::core::internal::CreateFourCC('A', 'B', 'C', 'D');

  REQUIRE(FeatureId{0x44434241} == value);
}

DatadogConfiguration CreateMockConfig() {
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
  auto config = DatadogConfiguration{
      TrackingConsent::Granted,
      GenerateRandomString(),
      GenerateRandomString(),
      GenerateRandomString(),
  };
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

  return config;
}

TEST_CASE("M return null for unregistered feature W GetFeature", "[core]") {
  // Given
  auto config = CreateMockConfig();
  auto core = DatadogCore::Create(config);

  // When
  auto feature = core->GetFeature<MockFeature>();

  // Then
  REQUIRE(feature == nullptr);
}

TEST_CASE("M return registered feature W RegisterFeature", "[core]") {
  // Given
  auto config = CreateMockConfig();
  auto mock_file_system = std::make_shared<MockDatadogFileSystem>();
  ALLOW_CALL(*mock_file_system, CreateChildFileSystem(_))
      .RETURN(mock_file_system);
  config.file_system = mock_file_system;
  auto core = DatadogCore::Create(config);
  core->RegisterFeature<MockFeature>();

  // When
  auto feature = core->GetFeature<MockFeature>();

  // Then
  REQUIRE(feature != nullptr);
}

TEST_CASE("M not overwrite registered feature W second register", "[core]") {
  // Given
  auto config = CreateMockConfig();
  auto mock_file_system = std::make_shared<MockDatadogFileSystem>();
  ALLOW_CALL(*mock_file_system, CreateChildFileSystem(_))
      .RETURN(mock_file_system);
  config.file_system = mock_file_system;
  auto core = DatadogCore::Create(config);
  core->RegisterFeature<MockFeature>();
  auto feature = core->GetFeature<MockFeature>();

  // When
  core->RegisterFeature<MockFeature>();
  auto second_feature = core->GetFeature<MockFeature>();

  REQUIRE(feature == second_feature);
}

TEST_CASE(
    "M create child file system for featre W DatadogCore::RegisterFeature",
    "[core]") {
  // Given
  auto config = CreateMockConfig();
  auto mock_file_system = std::make_shared<MockDatadogFileSystem>();
  config.file_system = mock_file_system;

  auto core = DatadogCore::Create(config);

  // Expect
  auto child_file_system = std::make_shared<MockDatadogFileSystem>();
  REQUIRE_CALL(*mock_file_system, CreateChildFileSystem("mock"))
      .LR_RETURN(child_file_system);

  // When
  core->RegisterFeature<MockFeature>();
}

TEST_CASE("M write to child file system for feature W SendMessage", "[core]") {
  // Given
  auto config = CreateMockConfig();
  auto mock_file_system = std::make_shared<MockDatadogFileSystem>();
  auto child_file_system = std::make_shared<MockDatadogFileSystem>();
  REQUIRE_CALL(*mock_file_system, CreateChildFileSystem("mock"))
      .LR_RETURN(child_file_system);
  ALLOW_CALL(*child_file_system, Exists(_)).RETURN(false);
  config.file_system = mock_file_system;

  auto core = DatadogCore::Create(config);
  core->RegisterFeature<MockFeature>();

  // Expect
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  REQUIRE_CALL(*child_file_system, Open(_)).LR_RETURN(std::move(mock_file));
  // Check that we write the message, but the surrounding format is tested
  // by FeatureStorage.
  REQUIRE_CALL(*mock_file, Write("feature message")).RETURN(true);
  ALLOW_CALL(*mock_file, Write(ne("feature message"))).RETURN(true);

  // When
  CoreMessage msg{{}, "feature message"};
  core->SendMessage(MockFeature::feature_id, std::move(msg));
}

}  // namespace
