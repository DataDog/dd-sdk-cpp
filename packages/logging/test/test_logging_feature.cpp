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
using datadog::core::Nanoseconds;
using datadog::core::UploadFrequency;
using datadog::core::internal::CoreContext;
using datadog::core::internal::PerformancePreset;
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
      : performance_preset_{
            BatchSize::Small,
            UploadFrequency::Frequent,
            BatchProcessingLevel::Medium,
        }, 
        core_{std::make_shared<MockDatadogCore>()},
        logging_{std::make_shared<DatadogLogging>(core_)} {}

 protected:
  PerformancePreset performance_preset_;
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
  CoreContext context{GenerateRandomString(), GenerateRandomString(), ""sv};
  ALLOW_CALL(*core_, GetNow()).RETURN(Nanoseconds{DefaultDateTimeProvider()});
  ALLOW_CALL(*core_, GetContext()).RETURN(context);

  // Expect
  REQUIRE_CALL(*core_, SendMessage(DatadogLogging::feature_id, _)).TIMES(1);

  // When
  logger->Log(LogLevel::Info, "logging message");
}

TEST_CASE_METHOD(LoggingTestFixture,
                 "M send LogEvent JSON to SendMessage W DatadogLogging::Log") {
  // Given
  DatadogLogConfiguration config{};
  auto logger = logging_->CreateLogger(config);
  CoreContext context{GenerateRandomString(), GenerateRandomString(), ""sv};
  ALLOW_CALL(*core_, GetNow()).RETURN(Nanoseconds{DefaultDateTimeProvider()});
  ALLOW_CALL(*core_, GetContext()).RETURN(context);

  // Expect
  std::string data;
  REQUIRE_CALL(*core_, SendMessage(DatadogLogging::feature_id, _))
      .LR_SIDE_EFFECT(data = _2.data())
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
