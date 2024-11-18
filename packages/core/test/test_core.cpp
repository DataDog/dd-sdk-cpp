// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include <datadog/core.h>
#include <datadog/internal/utils.h>

#include <trompeloeil.hpp>

#include "datadog/datadog_test.h"
#include "datadog/internal/core_internal.h"
#include "reporting/mock_datadog_reporter.h"
#include "storage/mock_datadog_file_system.h"

// Magic numbers okay in test files
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

namespace {

using datadog::core::CoreMessage;
using datadog::core::DatadogConfiguration;
using datadog::core::DatadogCore;
using datadog::core::DatadogFeature;
using datadog::core::FeatureId;
using datadog::core::Site;
using datadog::core::TrackingConsent;
using datadog::core::internal::DatadogCoreInternal;
using datadog::core::reporting::DatadogReporter;
using datadog::core::reporting::Report;
using datadog::core::reporting::mocks::MockDatadogReporter;
using datadog::core::storage::DatadogFileStatus;
using datadog::core::storage::TLVFileReader;
using datadog::core::storage::mocks::MockDatadogFile;
using datadog::core::storage::mocks::MockDatadogFileSystem;
using datadog::test::GenerateRandomString;

using trompeloeil::_;
using trompeloeil::ne;

using namespace std::chrono_literals;

class MockFeature : public DatadogFeature {
 public:
  explicit MockFeature(const std::weak_ptr<DatadogCore>& core) : core_(core) {}

  std::string_view GetName() const override { return "mock"; }

  Report CreateReportFromBatch(
      [[maybe_unused]] TLVFileReader& batch_file) const override {
    return Report("path");
  }

  static constexpr FeatureId feature_id =
      datadog::core::internal::CreateFourCC('M', 'O', 'C', 'K');

  std::weak_ptr<DatadogCore> core_;
};

TEST_CASE("M return proper value W four_cc", "[core]") {
  // Given
  auto value = datadog::core::internal::CreateFourCC('A', 'B', 'C', 'D');

  REQUIRE(FeatureId{0x44434241} == value);
}

std::unique_ptr<DatadogReporter> CreateMockReporter(std::string_view host) {
  return std::make_unique<MockDatadogReporter>(host);
}

DatadogConfiguration CreateMockConfig() {
  auto config = DatadogConfiguration{
      TrackingConsent::Granted, Site::us1,
      GenerateRandomString(),   GenerateRandomString(),
      GenerateRandomString(),
  };
  config.reporter_create_func = CreateMockReporter;

  return config;
}

TEST_CASE("M return null for unregistered feature W GetFeature", "[core]") {
  // Given
  auto config = CreateMockConfig();
  auto core = DatadogCoreInternal::Create(config);

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
  auto core = DatadogCoreInternal::Create(config);
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
  auto core = DatadogCoreInternal::Create(config);
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

  auto core = DatadogCoreInternal::Create(config);

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
  ALLOW_CALL(*child_file_system, ListFiles(_, _)).RETURN(true);
  config.file_system = mock_file_system;

  auto core = DatadogCoreInternal::Create(config);
  core->RegisterFeature<MockFeature>();

  // Expect
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  REQUIRE_CALL(*child_file_system, Open(_)).LR_RETURN(std::move(mock_file));
  // Check that we write the message, but the surrounding format is tested
  // by FeatureStorage.
  REQUIRE_CALL(*mock_file, Write("feature message")).RETURN(true);
  ALLOW_CALL(*mock_file, Write(ne("feature message"))).RETURN(true);

  // When
  core->Start();
  CoreMessage msg{MockFeature::feature_id, {}, "feature message"};
  core->SendMessage(std::move(msg));
  core->Shutdown();
}

TEST_CASE("M send batches to reporter W ListFiles", "[core]") {
  // Note: this is more of an integration test. Further tests for the reporting
  // thread logic live in internal/test_reporting_thread

  // Given
  auto config = CreateMockConfig();
  config.reporter_create_func = CreateMockReporter;
  auto mock_file_system = std::make_shared<MockDatadogFileSystem>();
  auto child_file_system = std::make_shared<MockDatadogFileSystem>();
  REQUIRE_CALL(*mock_file_system, CreateChildFileSystem("mock"))
      .LR_RETURN(child_file_system);
  ALLOW_CALL(*child_file_system, Exists(_)).RETURN(false);
  ALLOW_CALL(*child_file_system, Delete(_)).RETURN(DatadogFileStatus::Ok);
  config.file_system = mock_file_system;

  auto core = DatadogCoreInternal::Create(config);
  auto mock_feature = core->RegisterFeature<MockFeature>();
  core->CreateReporter();
  auto reporter =
      std::dynamic_pointer_cast<MockDatadogReporter>(core->GetReporter());

  // Expect
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  REQUIRE_CALL(*child_file_system, ListFiles("", _))
      .LR_SIDE_EFFECT(_2.push_back("any"))
      .RETURN(true);
  REQUIRE_CALL(*child_file_system, Open(_)).LR_RETURN(std::move(mock_file));
  std::condition_variable cv;
  std::mutex mutex;
  REQUIRE_CALL(*reporter, Send(_))
      .LR_SIDE_EFFECT(cv.notify_all())
      .RETURN(DatadogReporter::Status::Ok);

  // When
  core->Start();

  // Then
  {
    std::unique_lock<std::mutex> lock(mutex);
    auto did_timeout = cv.wait_for(lock, 200ms);
    REQUIRE(did_timeout == std::cv_status::no_timeout);
  }
  core->Shutdown();
}

}  // namespace

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
