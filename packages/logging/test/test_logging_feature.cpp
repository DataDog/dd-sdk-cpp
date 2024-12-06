// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/logging.h"

#include <rapidjson/document.h>

#include "datadog/datadog_test.h"
#include "datadog/mock_datadog_core.h"

namespace {

using namespace std::string_view_literals;

using datadog::core::BatchProcessingLevel;
using datadog::core::BatchSize;
using datadog::core::DatadogCore;
using datadog::core::DefaultDateTimeProvider;
using datadog::core::FeatureId;
using datadog::core::Nanoseconds;
using datadog::core::UploadFrequency;
using datadog::core::internal::CoreContext;
using datadog::core::mocks::MockDatadogCore;
using datadog::logging::DatadogLogConfiguration;
using datadog::logging::DatadogLogging;
using datadog::logging::LogLevel;
using datadog::test::GenerateRandomString;
using rapidjson::Document;
using rapidjson::kParseErrorNone;

using trompeloeil::_;

class LoggingTestFixture {
 public:
  LoggingTestFixture()
      : core_{MockDatadogCore::Create()},
        logging_{std::make_shared<DatadogLogging>(core_)} {}

 protected:
  std::shared_ptr<MockDatadogCore> core_;
  std::shared_ptr<DatadogLogging> logging_;
};

TEST_CASE_METHOD(LoggingTestFixture,
                 "M create logger W LoggingFeature::CreateLogger",
                 "[logging]") {
  // Given
  DatadogLogConfiguration config{};

  // When
  auto logger = logging_->CreateLogger(config);

  // Then
  REQUIRE(logger);
}

TEST_CASE_METHOD(LoggingTestFixture,
                 "M send message to Core W DatadogLogger::Log") {
  // Given
  DatadogLogConfiguration config{};
  auto logger = logging_->CreateLogger(config);
  CoreContext context{
      GenerateRandomString(), GenerateRandomString(), GenerateRandomString(),
      GenerateRandomString(), GenerateRandomString(),
  };
  ALLOW_CALL(*core_, GetNow()).RETURN(Nanoseconds{DefaultDateTimeProvider()});
  ALLOW_CALL(*core_, GetContext()).RETURN(context);

  // Expect
  FeatureId feature_id{0};
  REQUIRE_CALL(*core_, SendMessage(_))
      .LR_SIDE_EFFECT(feature_id = _1.feature_id())
      .TIMES(1);

  // When
  logger->Log(LogLevel::Info, "logging message");

  // Then
  REQUIRE(feature_id == DatadogLogging::feature_id);
}

TEST_CASE_METHOD(LoggingTestFixture,
                 "M send LogEvent JSON to SendMessage W DatadogLogging::Log") {
  // Given
  DatadogLogConfiguration config{};
  auto logger = logging_->CreateLogger(config);
  CoreContext context{
      GenerateRandomString(), GenerateRandomString(), GenerateRandomString(),
      GenerateRandomString(), GenerateRandomString(),
  };
  ALLOW_CALL(*core_, GetNow()).RETURN(Nanoseconds{DefaultDateTimeProvider()});
  ALLOW_CALL(*core_, GetContext()).RETURN(context);

  // Expect
  std::string data;
  REQUIRE_CALL(*core_, SendMessage(_))
      .LR_SIDE_EFFECT(data = _1.data())
      .TIMES(1);

  // When
  logger->Log(LogLevel::Info, "logging message");

  // Then
  REQUIRE(!data.empty());
  Document d;
  d.Parse(data.c_str());
  REQUIRE(d.GetParseError() == kParseErrorNone);
  REQUIRE(d["status"] == "info");
  REQUIRE(d["message"] == "logging message");
}

// TODO(jeff.ward): Add more complete logging test, including that context is
// forwarded properly, time is serialized correctly, and log levels are
// serialized properly.

}  // namespace
