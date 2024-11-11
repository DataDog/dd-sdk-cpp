// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/internal/reporting_thread.h"

#include "datadog/datadog_test.h"
#include "internal/mock_datadog_core_internal.h"
#include "reporting/mock_datadog_reporter.h"
#include "storage/mock_datadog_file_system.h"

#include <catch2/trompeloeil.hpp>

namespace {

using datadog::core::BatchProcessingLevel;
using datadog::core::BatchSize;
using datadog::core::DatadogCore;
using datadog::core::DatadogFeature;
using datadog::core::FeatureId;
using datadog::core::UploadFrequency;
using datadog::core::internal::PerformancePreset;
using datadog::core::internal::ReportingThread;
using datadog::core::mocks::MockDatadogCoreInternal;
using datadog::core::reporting::DatadogReporter;
using datadog::core::reporting::Report;
using datadog::core::reporting::mocks::MockDatadogReporter;
using datadog::core::storage::DatadogFileStatus;
using datadog::core::storage::TLVFileReader;
using datadog::core::storage::mocks::MockDatadogFile;
using datadog::core::storage::mocks::MockDatadogFileSystem;

using trompeloeil::_;

class MockFeature : public DatadogFeature {
 public:
  explicit MockFeature(const std::shared_ptr<DatadogCore>& core,
                       std::string_view name = "mock")
      : name_{name}, core_(core) {}

  std::string_view GetName() const override { return name_; }

  MAKE_MOCK1(CreateReportFromBatch,
             Report(TLVFileReader& batch_file),
             const override);

  static constexpr FeatureId feature_id =
      datadog::core::internal::CreateFourCC('M', 'O', 'C', 'K');

  std::string_view name_;
  std::weak_ptr<DatadogCore> core_;
};

class ReportingThreadFixture {
 public:
  ReportingThreadFixture()
      : core_{MockDatadogCoreInternal::Create()},
        feature_{std::make_shared<MockFeature>(core_)},
        feature_file_system_{std::make_shared<MockDatadogFileSystem>()},
        reporter_{std::make_shared<MockDatadogReporter>("any")} {}

  std::shared_ptr<MockDatadogCoreInternal> core_;
  std::shared_ptr<MockFeature> feature_;
  std::shared_ptr<MockDatadogFileSystem> feature_file_system_;
  std::shared_ptr<MockDatadogReporter> reporter_;
};

// NOLINTNEXTLINE(google-readability-function-size)
TEST_CASE_METHOD(ReportingThreadFixture,
                 "ReportingThread with one feature",
                 "[reporting_thread]") {
  // Given
  REQUIRE_CALL(*(core_->file_system), CreateChildFileSystem("mock"))
      .LR_RETURN(feature_file_system_);
  ALLOW_CALL(*feature_file_system_, Exists(_)).RETURN(false);
  ALLOW_CALL(*feature_file_system_, Delete(_)).RETURN(DatadogFileStatus::Ok);
  ALLOW_CALL(*core_, GetReporter())
      .LR_RETURN(std::dynamic_pointer_cast<DatadogReporter>(reporter_));

  core_->RegisterFeature(MockFeature::feature_id, feature_);

  SECTION("M send no reports W no files exist") {
    // Given
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .SIDE_EFFECT(_2.clear())
        .RETURN(true);

    ReportingThread thread{core_, core_->performance_preset};

    // When
    thread.SingleReportingFrame();

    // Then - No calls on reporter
  }

  SECTION("M send report from feature W file exits") {
    // Given
    std::unique_ptr<MockDatadogFile> file =
        std::make_unique<MockDatadogFile>("any");
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .LR_SIDE_EFFECT(_2.push_back("any"))
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_, Open("any")).LR_RETURN(std::move(file));

    ReportingThread thread{core_, core_->performance_preset};

    // Expect
    Report report = Report{"any"};
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .RETURN(DatadogReporter::Status::Ok);
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .LR_RETURN(std::move(report));

    // When
    thread.SingleReportingFrame();
  }

  SECTION("M send report from feature W multiple files exits") {
    // Given
    std::unique_ptr<MockDatadogFile> file_1 =
        std::make_unique<MockDatadogFile>("any");
    std::unique_ptr<MockDatadogFile> file_2 =
        std::make_unique<MockDatadogFile>("any_2");
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .LR_SIDE_EFFECT({
          _2.push_back("any");
          _2.push_back("any_2");
        })
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_, Open("any")).LR_RETURN(std::move(file_1));
    ALLOW_CALL(*feature_file_system_, Open("any_2"))
        .LR_RETURN(std::move(file_2));

    ReportingThread thread{core_, core_->performance_preset};

    // Expect
    Report report = Report{"any_path"};
    Report report2 = Report{"any_2"};
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .RETURN(DatadogReporter::Status::Ok);
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report2.GetPath())
        .RETURN(DatadogReporter::Status::Ok);
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .LR_RETURN(std::move(report));
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any_2")
        .LR_RETURN(std::move(report2));

    // When
    thread.SingleReportingFrame();
  }

  SECTION(
      "M limit number of files based on performance preset W multiple files "
      "exits") {
    // Given
    std::unique_ptr<MockDatadogFile> file_1 =
        std::make_unique<MockDatadogFile>("any");
    std::unique_ptr<MockDatadogFile> file_2 =
        std::make_unique<MockDatadogFile>("any_2");
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .LR_SIDE_EFFECT({
          _2.push_back("any");
          _2.push_back("any_2");
          _2.push_back("any_3");
        })
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_, Open("any")).LR_RETURN(std::move(file_1));
    ALLOW_CALL(*feature_file_system_, Open("any_2"))
        .LR_RETURN(std::move(file_2));

    // Modified performance preset
    auto performance_preset =
        PerformancePreset(core_->performance_preset.max_file_size(),
                          core_->performance_preset.max_directory_size(),
                          core_->performance_preset.max_file_age_for_write(),
                          core_->performance_preset.min_file_age_for_read(),
                          core_->performance_preset.initial_upload_delay(),
                          core_->performance_preset.min_upload_delay(),
                          core_->performance_preset.max_upload_delay(), 2);
    ReportingThread thread{core_, performance_preset};

    // Expect
    Report report = Report{"any_path"};
    Report report2 = Report{"any_2"};
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .RETURN(DatadogReporter::Status::Ok);
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report2.GetPath())
        .RETURN(DatadogReporter::Status::Ok);
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .LR_RETURN(std::move(report));
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any_2")
        .LR_RETURN(std::move(report2));

    // When
    thread.SingleReportingFrame();
  }

  SECTION("M delete batches W send of report succeeds") {
    // Given
    std::unique_ptr<MockDatadogFile> file_1 =
        std::make_unique<MockDatadogFile>("any");
    std::unique_ptr<MockDatadogFile> file_2 =
        std::make_unique<MockDatadogFile>("any_2");
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .LR_SIDE_EFFECT({
          _2.push_back("any");
          _2.push_back("any_2");
        })
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_, Open("any")).LR_RETURN(std::move(file_1));
    ALLOW_CALL(*feature_file_system_, Open("any_2"))
        .LR_RETURN(std::move(file_2));

    ReportingThread thread{core_, core_->performance_preset};

    // Expect
    Report report = Report{"any_path"};
    Report report2 = Report{"any_2"};
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .RETURN(DatadogReporter::Status::Ok);
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report2.GetPath())
        .RETURN(DatadogReporter::Status::Ok);
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .LR_RETURN(std::move(report));
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any_2")
        .LR_RETURN(std::move(report2));
    REQUIRE_CALL(*feature_file_system_, Delete("any"))
        .RETURN(DatadogFileStatus::Ok);
    REQUIRE_CALL(*feature_file_system_, Delete("any_2"))
        .RETURN(DatadogFileStatus::Ok);

    // When
    thread.SingleReportingFrame();
  }

  SECTION("M stop frame W send of requires retry") {
    // Given
    std::unique_ptr<MockDatadogFile> file_1 =
        std::make_unique<MockDatadogFile>("any");
    std::unique_ptr<MockDatadogFile> file_2 =
        std::make_unique<MockDatadogFile>("any_2");
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .LR_SIDE_EFFECT({
          _2.push_back("any");
          _2.push_back("any_2");
        })
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_, Open("any")).LR_RETURN(std::move(file_1));

    ReportingThread thread{core_, core_->performance_preset};

    // Expect
    Report report = Report{"any_path"};
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .RETURN(DatadogReporter::Status::ErrorNeedsRetry);
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .LR_RETURN(std::move(report));
    FORBID_CALL(*feature_file_system_, Delete(_));

    // When
    thread.SingleReportingFrame();
  }

  SECTION("M send same report after retry failure W calling next frame") {
    // Given
    auto file_1a = std::make_unique<MockDatadogFile>("any");
    auto file_1b = std::make_unique<MockDatadogFile>("any");
    auto file_2 = std::make_unique<MockDatadogFile>("any_2");
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .LR_SIDE_EFFECT({
          _2.push_back("any");
          _2.push_back("any_2");
        })
        .RETURN(true);

    ReportingThread thread{core_, core_->performance_preset};

    // Expect
    trompeloeil::sequence seq;
    Report report = Report{"any_1"};
    Report report2 = Report{"any_2"};
    Report report3 = Report{"any_3"};
    REQUIRE_CALL(*feature_file_system_, Open("any"))
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(file_1a));
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(report));
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .IN_SEQUENCE(seq)
        .RETURN(DatadogReporter::Status::ErrorNeedsRetry);
    REQUIRE_CALL(*feature_file_system_, Open("any"))
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(file_1b));
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(report2));
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report2.GetPath())
        .IN_SEQUENCE(seq)
        .RETURN(DatadogReporter::Status::Ok);
    REQUIRE_CALL(*feature_file_system_, Open("any_2"))
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(file_2));
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any_2")
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(report3));
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report3.GetPath())
        .IN_SEQUENCE(seq)
        .RETURN(DatadogReporter::Status::Ok);

    // When
    thread.SingleReportingFrame();
    thread.SingleReportingFrame();
  }

  SECTION("M delete file W send returns unrecoverable error") {
    // Given
    std::unique_ptr<MockDatadogFile> file_1 =
        std::make_unique<MockDatadogFile>("any");
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .LR_SIDE_EFFECT({ _2.push_back("any"); })
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_, Open("any")).LR_RETURN(std::move(file_1));

    ReportingThread thread{core_, core_->performance_preset};

    // Expect
    Report report = Report{"any_path"};
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .LR_RETURN(std::move(report));
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .RETURN(DatadogReporter::Status::UnrecoverableError);
    REQUIRE_CALL(*feature_file_system_, Delete("any"))
        .RETURN(DatadogFileStatus::Ok);

    // When
    thread.SingleReportingFrame();
  }

  SECTION("M continue processing files W send returns unrecoverable error") {
    // Given
    std::unique_ptr<MockDatadogFile> file_1 =
        std::make_unique<MockDatadogFile>("any");
    auto file_2 = std::make_unique<MockDatadogFile>("any_2");
    ALLOW_CALL(*feature_file_system_, ListFiles("", _))
        .LR_SIDE_EFFECT({
          _2.push_back("any");
          _2.push_back("any_2");
        })
        .RETURN(true);

    ReportingThread thread{core_, core_->performance_preset};

    // Expect
    trompeloeil::sequence seq;
    Report report = Report{"any_1"};
    Report report2 = Report{"any_2"};
    REQUIRE_CALL(*feature_file_system_, Open("any"))
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(file_1));
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any")
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(report));
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .IN_SEQUENCE(seq)
        .RETURN(DatadogReporter::Status::UnrecoverableError);
    REQUIRE_CALL(*feature_file_system_, Open("any_2"))
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(file_2));
    REQUIRE_CALL(*feature_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any_2")
        .IN_SEQUENCE(seq)
        .LR_RETURN(std::move(report2));
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report2.GetPath())
        .IN_SEQUENCE(seq)
        .RETURN(DatadogReporter::Status::Ok);

    // When
    thread.SingleReportingFrame();
  }
}

class ReportingThreadMultipleFeaturesFixture {
 public:
  ReportingThreadMultipleFeaturesFixture()
      : core_{MockDatadogCoreInternal::Create()},
        feature_a_{std::make_shared<MockFeature>(core_)},
        feature_b_{std::make_shared<MockFeature>(core_, "mock2")},
        feature_file_system_a_{std::make_shared<MockDatadogFileSystem>()},
        feature_file_system_b_{std::make_shared<MockDatadogFileSystem>()},
        reporter_{std::make_shared<MockDatadogReporter>("any")} {}

  std::shared_ptr<MockDatadogCoreInternal> core_;
  std::shared_ptr<MockFeature> feature_a_;
  std::shared_ptr<MockFeature> feature_b_;
  std::shared_ptr<MockDatadogFileSystem> feature_file_system_a_;
  std::shared_ptr<MockDatadogFileSystem> feature_file_system_b_;
  std::shared_ptr<MockDatadogReporter> reporter_;
};

TEST_CASE_METHOD(ReportingThreadMultipleFeaturesFixture,
                 "ReportingThread with multiple features",
                 "[reporting_thread]") {
  // Given
  REQUIRE_CALL(*(core_->file_system), CreateChildFileSystem("mock"))
      .LR_RETURN(feature_file_system_a_);
  REQUIRE_CALL(*(core_->file_system), CreateChildFileSystem("mock2"))
      .LR_RETURN(feature_file_system_b_);
  ALLOW_CALL(*feature_file_system_a_, Exists(_)).RETURN(false);
  ALLOW_CALL(*feature_file_system_a_, Delete(_)).RETURN(DatadogFileStatus::Ok);
  ALLOW_CALL(*feature_file_system_b_, Exists(_)).RETURN(false);
  ALLOW_CALL(*feature_file_system_b_, Delete(_)).RETURN(DatadogFileStatus::Ok);
  ALLOW_CALL(*core_, GetReporter())
      .LR_RETURN(std::dynamic_pointer_cast<DatadogReporter>(reporter_));

  core_->RegisterFeature(MockFeature::feature_id, feature_a_);
  // Second feature id
  FeatureId second_feature_id =
      datadog::core::internal::CreateFourCC('M', 'O', 'K', '2');
  core_->RegisterFeature(second_feature_id, feature_b_);

  SECTION("M send reports from all features W reporting frame") {
    // Given
    ALLOW_CALL(*feature_file_system_a_, ListFiles("", _))
        .SIDE_EFFECT(_2.push_back("any_a"))
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_b_, ListFiles("", _))
        .SIDE_EFFECT(_2.push_back("any_b"))
        .RETURN(true);

    ReportingThread thread{core_, core_->performance_preset};
    std::unique_ptr<MockDatadogFile> file_a =
        std::make_unique<MockDatadogFile>("any_a");
    Report report_a = Report{"any_a"};
    REQUIRE_CALL(*feature_file_system_a_, Open("any_a"))
        .LR_RETURN(std::move(file_a));
    REQUIRE_CALL(*feature_a_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any_a")
        .LR_RETURN(std::move(report_a));
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report_a.GetPath())
        .RETURN(DatadogReporter::Status::Ok);

    std::unique_ptr<MockDatadogFile> file_b =
        std::make_unique<MockDatadogFile>("any_b");
    Report report_b = Report{"any_b"};
    REQUIRE_CALL(*feature_file_system_b_, Open("any_b"))
        .LR_RETURN(std::move(file_b));
    REQUIRE_CALL(*feature_b_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any_b")
        .LR_RETURN(std::move(report_b));
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report_b.GetPath())
        .RETURN(DatadogReporter::Status::Ok);

    // When
    thread.SingleReportingFrame();
  }

  SECTION("M stop frame for all features W send of requires retry") {
    // Given
    std::unique_ptr<MockDatadogFile> file_1 =
        std::make_unique<MockDatadogFile>("any_b");
    ALLOW_CALL(*feature_file_system_a_, ListFiles("", _))
        .LR_SIDE_EFFECT({
          _2.push_back("any_a");
          _2.push_back("any_a_2");
        })
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_b_, ListFiles("", _))
        .LR_SIDE_EFFECT({ _2.push_back("any_b"); })
        .RETURN(true);
    ALLOW_CALL(*feature_file_system_b_, Open("any_b"))
        .LR_RETURN(std::move(file_1));

    ReportingThread thread{core_, core_->performance_preset};

    // Expect - We know feature_b is the first one in the list
    Report report = Report{"any_path"};
    REQUIRE_CALL(*reporter_, Send(_))
        .LR_WITH(_1.GetPath() == report.GetPath())
        .RETURN(DatadogReporter::Status::ErrorNeedsRetry);
    REQUIRE_CALL(*feature_b_, CreateReportFromBatch(_))
        .WITH(_1.GetPath() == "any_b")
        .LR_RETURN(std::move(report));
    FORBID_CALL(*feature_file_system_a_, Delete(_));
    FORBID_CALL(*feature_file_system_b_, Delete(_));

    // When
    thread.SingleReportingFrame();
  }
}
}  // namespace
