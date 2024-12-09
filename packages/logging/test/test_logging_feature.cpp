// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/logging.h"

#include <tuple>

#include <rapidjson/document.h>

#include "datadog/datadog_test.h"
#include "datadog/mock_datadog_core.h"

namespace {

// NOLINTBEGIN:(cppcoreguidelines-avoid-magic-numbers)

using namespace std::string_view_literals;

using datadog::core::BatchProcessingLevel;
using datadog::core::BatchSize;
using datadog::core::DatadogAttribute;
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

TEST_CASE_METHOD(LoggingTestFixture, "Simple logging tests", "[logging]") {
  // Given
  DatadogLogConfiguration config{};
  auto logger = logging_->CreateLogger(config);
  CoreContext context{
      GenerateRandomString(), GenerateRandomString(), GenerateRandomString(),
      GenerateRandomString(), GenerateRandomString(),
  };
  ALLOW_CALL(*core_, GetNow()).RETURN(Nanoseconds{DefaultDateTimeProvider()});
  ALLOW_CALL(*core_, GetContext()).RETURN(context);

  SECTION("M send message to Core W DatadogLogger::Log") {
    // Expect
    FeatureId feature_id{0};
    REQUIRE_CALL(*core_, SendMessage(_))
        .LR_SIDE_EFFECT(feature_id = _1.feature_id())
        .TIMES(1);

    // When
    logger->Log(LogLevel::Info, "logging message", DatadogAttribute::kNull);

    // Then
    REQUIRE(feature_id == DatadogLogging::feature_id);
  }

  SECTION("M send LogEvent JSON to SendMessage W DatadogLogger::Log") {
    // Expect
    std::string data;
    REQUIRE_CALL(*core_, SendMessage(_))
        .LR_SIDE_EFFECT(data = _1.data())
        .TIMES(1);

    // When
    logger->Log(LogLevel::Info, "logging message", DatadogAttribute::kNull);

    // Then
    REQUIRE(!data.empty());
    Document d;
    d.Parse(data.c_str());
    REQUIRE(d.GetParseError() == kParseErrorNone);
    REQUIRE(d["status"] == "info");
    REQUIRE(d["message"] == "logging message");
  }

  SECTION(
      "M send proper level in LogEvent JSON to SendMessage W "
      "DatadogLogger::Log") {
    // Given
    std::array levels{
        std::tuple{LogLevel::Debug, "debug"sv},
        std::tuple{LogLevel::Info, "info"sv},
        std::tuple{LogLevel::Notice, "notice"sv},
        std::tuple{LogLevel::Warn, "warn"sv},
        std::tuple{LogLevel::Error, "error"sv},
        std::tuple{LogLevel::Critical, "critical"sv},
    };

    // Expect
    std::vector<std::string> message_datas;
    REQUIRE_CALL(*core_, SendMessage(_))
        .LR_SIDE_EFFECT(message_datas.push_back(_1.data()))
        .TIMES(levels.size());

    // When
    for (const auto& [level, _] : levels) {
      logger->Log(level, "logging message", DatadogAttribute::kNull);
    }

    // Then
    REQUIRE(message_datas.size() == 6);
    for (int i = 0; i < 6; ++i) {
      auto [_, level_name] = levels[i];
      const auto& data = message_datas[i];
      Document d;
      d.Parse(data.c_str());
      REQUIRE(d.GetParseError() == kParseErrorNone);
      REQUIRE(d["status"].GetString() == level_name);
      REQUIRE(d["message"] == "logging message");
    }
  }

  SECTION("M send log attributes as JSON to SendMessage W DatadogLogger::Log") {
    // Given
    DatadogAttribute log_attr{DatadogAttribute::Type::Object, 2};
    log_attr.SetMember("test_member", DatadogAttribute("test_value"));
    log_attr.SetMember("test_int_member", DatadogAttribute(1885));

    // Expect
    std::string data;
    REQUIRE_CALL(*core_, SendMessage(_))
        .LR_SIDE_EFFECT(data = _1.data())
        .TIMES(1);

    // When
    logger->Log(LogLevel::Info, "logging message", log_attr);

    // Then
    REQUIRE(!data.empty());
    Document d;
    d.Parse(data.c_str());
    REQUIRE(d.GetParseError() == kParseErrorNone);
    REQUIRE(d["test_member"] == "test_value");
    REQUIRE(d["test_int_member"] == 1885);
  }

  SECTION(
      "M send global log attributes as JSON to SendMessage w "
      "DatadogLogger::Log") {
    // Given
    logging_->AddAttribute("global_member", DatadogAttribute("test_value"));
    logging_->AddAttribute("global_int_member", DatadogAttribute(-85));

    // Expect
    std::string data;
    REQUIRE_CALL(*core_, SendMessage(_))
        .LR_SIDE_EFFECT(data = _1.data())
        .TIMES(1);

    // When
    logger->Log(LogLevel::Warn, "logging message", DatadogAttribute::kNull);

    // Then
    REQUIRE(!data.empty());
    Document d;
    d.Parse(data.c_str());
    REQUIRE(d.GetParseError() == kParseErrorNone);
    REQUIRE(d["global_member"] == "test_value");
    REQUIRE(d["global_int_member"] == -85);
  }

  SECTION(
      "M send logger attributes as JSON to SendMessage w "
      "DatadogLogger::Log") {
    // Given
    logger->AddAttribute("logger_member", DatadogAttribute("test_value"));
    logger->AddAttribute("logger_int_member", DatadogAttribute(11177712));

    // Expect
    std::string data;
    REQUIRE_CALL(*core_, SendMessage(_))
        .LR_SIDE_EFFECT(data = _1.data())
        .TIMES(1);

    // When
    logger->Log(LogLevel::Warn, "logging message", DatadogAttribute::kNull);

    // Then
    REQUIRE(!data.empty());
    Document d;
    d.Parse(data.c_str());
    REQUIRE(d.GetParseError() == kParseErrorNone);
    REQUIRE(d["logger_member"] == "test_value");
    REQUIRE(d["logger_int_member"] == 11177712);
  }

  SECTION(
      "M overwrite logger and global attributes with log attributes W "
      "DatadogLogger::Log") {
    // Given
    logging_->AddAttribute("member_a", DatadogAttribute("member_a_value"));
    logging_->AddAttribute("member_b", DatadogAttribute(3.5511));
    logging_->AddAttribute("member_c", DatadogAttribute(-994817));
    logger->AddAttribute("member_a", DatadogAttribute("logger_override_1"));
    logger->AddAttribute("member_b", DatadogAttribute("logger_override_b"));
    logger->AddAttribute("member_b.1", DatadogAttribute(11177712));
    logger->AddAttribute("member_b.2", DatadogAttribute(-11177712));

    // Expect
    std::string data;
    REQUIRE_CALL(*core_, SendMessage(_))
        .LR_SIDE_EFFECT(data = _1.data())
        .TIMES(1);

    // When
    DatadogAttribute attr{DatadogAttribute::Type::Object, 3};
    attr.SetMember("member_a", DatadogAttribute("my_log_override"));
    attr.SetMember("member_b.1", DatadogAttribute(999584));
    logger->Log(LogLevel::Warn, "logging message", attr);

    // Then
    REQUIRE(!data.empty());
    Document d;
    d.Parse(data.c_str());
    REQUIRE(d.GetParseError() == kParseErrorNone);
    REQUIRE(d["member_a"] == "my_log_override");
    REQUIRE(d["member_b"] == "logger_override_b");
    REQUIRE(d["member_c"] == -994817);
    REQUIRE(d["member_b.1"] == 999584);
    REQUIRE(d["member_b.2"] == -11177712);
  }
}

// TODO(jeff.ward): Add more complete logging test, including that context is
// forwarded properly, time is serialized correctly

// NOLINTEND:(cppcoreguidelines-avoid-magic-numbers)

}  // namespace
