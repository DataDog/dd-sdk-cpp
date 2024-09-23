// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/storage/feature_storage.h"

#include "datadog/core_configuration.h"
#include "datadog/storage/datadog_file_system.h"

#include "test.h"

namespace {

using datadog::core::BatchProcessingLevel;
using datadog::core::BatchSize;
using datadog::core::DefaultDateTimeProvider;
using datadog::core::UploadFrequency;
using datadog::core::internal::PerformancePreset;
using datadog::core::storage::FeatureStorage;
using datadog::core::storage::StdDatadogFileSystem;

class FeatureStorageFixture {
 public:
  FeatureStorageFixture() : performance_preset_{
    BatchSize::Small, UploadFrequency::Frequent, BatchProcessingLevel::Low,    
  },
  file_system_{std::make_shared<StdDatadogFileSystem>("datadog/storage_tests")} {
    ClearFileSystem();
  }
  ~FeatureStorageFixture() { ClearFileSystem(); }

 protected:
  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  PerformancePreset performance_preset_;
  std::shared_ptr<StdDatadogFileSystem> file_system_;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

 private:
  void ClearFileSystem() {
    // Delete any existing files
    auto files = file_system_->ListFilePaths("");
    for (const auto& file : files) {
      file_system_->DeleteFile(file);
    }
  }
};

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M create new file W Write",
                 "[feature_storage]") {
  // Given
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system_};

  // When
  feature_storage.Write("File contents");

  // Then
  auto files = file_system_->ListFilePaths("");
  REQUIRE(files.size() == 1);
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M name file based on date W Write",
                 "[feature_storage]") {
  // Given
  constexpr uint64_t fake_nanos = 123456789123;
  auto mock_time_provider = [] { return fake_nanos; };
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 mock_time_provider, file_system_};

  // When
  feature_storage.Write("File contents");

  // Then
  auto files = file_system_->ListFilePaths("");
  REQUIRE(files.size() == 1);
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
  auto time_ms = fake_nanos / 1000;
  REQUIRE(files.front() == std::to_string(time_ms));
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M write block to file W Wfite",
                 "[feature_storage]") {
  // White box test - the format of files is
  // Given
}

}  // namespace
