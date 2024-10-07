// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/storage/feature_storage.h"

#include "datadog/core_configuration.h"
#include "datadog/internal/utils.h"
#include "datadog/storage/datadog_file_system.h"

#include "mock_datadog_file_system.h"

// Magic numbers okay in test files
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

namespace {

using namespace std::literals::string_view_literals;

using datadog::core::BatchProcessingLevel;
using datadog::core::BatchSize;
using datadog::core::DefaultDateTimeProvider;
using datadog::core::UploadFrequency;
using datadog::core::internal::NanoToMillis;
using datadog::core::internal::PerformancePreset;
using datadog::core::storage::DatadogFileStatus;
using datadog::core::storage::FeatureStorage;
using datadog::core::storage::mocks::MockDatadogFile;
using datadog::core::storage::mocks::MockDatadogFileSystem;
using trompeloeil::_;
using trompeloeil::ne;

class FeatureStorageFixture {
 public:
  FeatureStorageFixture()
      : performance_preset_{
            BatchSize::Small,
            UploadFrequency::Frequent,
            BatchProcessingLevel::Low,
        } {}

 protected:
  PerformancePreset performance_preset_;
};

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M create new file W Write",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};
  ALLOW_CALL(*file_system, Exists(_)).RETURN(false);
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  ALLOW_CALL(*mock_file, Write(_)).RETURN(true);

  // Expect
  REQUIRE_CALL(*file_system, Open(_)).LR_RETURN(std::move(mock_file));

  // When
  REQUIRE(feature_storage.Write("File contents"));
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M name file based on date W Write",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  constexpr uint64_t fake_nanos = 123456789123L;
  auto mock_time_provider = [] { return fake_nanos; };
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 mock_time_provider, file_system};
  ALLOW_CALL(*file_system, Exists(_)).RETURN(false);
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  ALLOW_CALL(*mock_file, Write(_)).RETURN(true);

  // Expect
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
  auto str_time_ms = std::to_string(NanoToMillis(fake_nanos));
  REQUIRE_CALL(*file_system, Open(str_time_ms)).LR_RETURN(std::move(mock_file));

  // When
  REQUIRE(feature_storage.Write("File contents"));
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M request new file if file exists W Write",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  constexpr uint64_t fake_nanos = 123456789123;
  auto mock_time_provider = [] { return fake_nanos; };
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 mock_time_provider, file_system};
  auto existing_file_path =
      std::filesystem::path(std::to_string(NanoToMillis(fake_nanos)));
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  ALLOW_CALL(*mock_file, Write(_)).RETURN(true);

  // Expect
  REQUIRE_CALL(*file_system, Exists(existing_file_path)).RETURN(true);
  REQUIRE_CALL(*file_system,
               Exists(ne<std::filesystem::path>(existing_file_path)))
      .RETURN(false);

  std::filesystem::path file_path;
  REQUIRE_CALL(*file_system, Open(_))
      .LR_SIDE_EFFECT(file_path = _1)
      .LR_RETURN(std::move(mock_file));

  // When
  REQUIRE(feature_storage.Write("File contents"));

  // Then
  REQUIRE(file_path != existing_file_path);
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M give up looking for file if too many exist W Write",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  ALLOW_CALL(*mock_file, Write(_)).RETURN(true);

  // Expect
  REQUIRE_CALL(*file_system, Exists(_)).TIMES(0, 5).RETURN(true);

  // When
  REQUIRE(!feature_storage.Write("File contents"));
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M write block to file W Write",
                 "[feature_storage]") {
  // White box test - the format of files is part of
  // requirements Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};
  ALLOW_CALL(*file_system, Exists(_)).RETURN(false);
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  ALLOW_CALL(*file_system, Open(_)).LR_RETURN(std::move(mock_file));

  // Expect
  static constexpr std::string_view kExpectedHeader{"\0", sizeof(uint16_t)};
  static constexpr auto kExpectedContent = "File contents"sv;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
  static constexpr const size_t kSize[]{kExpectedContent.size()};
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  static const std::string_view kExpectedSize{
      reinterpret_cast<const char*>(kSize), sizeof(uint32_t)};
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  {
    trompeloeil::sequence seq;

    REQUIRE_CALL(*mock_file, Write(kExpectedHeader))
        .IN_SEQUENCE(seq)
        .RETURN(true);
    REQUIRE_CALL(*mock_file, Write(kExpectedSize))
        .IN_SEQUENCE(seq)
        .RETURN(true);
    REQUIRE_CALL(*mock_file, Write(kExpectedContent))
        .IN_SEQUENCE(seq)
        .RETURN(true);

    // When
    REQUIRE(feature_storage.Write(kExpectedContent));
  }
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M reuse same file when possible W Write ",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};
  ALLOW_CALL(*file_system, Exists(_)).RETURN(false);
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  ALLOW_CALL(*file_system, Open(_)).LR_RETURN(std::move(mock_file));

  // Expect
  static constexpr std::string_view kExpectedHeader{"\0", sizeof(uint16_t)};
  static constexpr auto kExpectedContent = "File contents"sv;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
  static constexpr const size_t kSize[]{kExpectedContent.size()};
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  static const std::string_view kExpectedSize{
      reinterpret_cast<const char*>(kSize), sizeof(uint32_t)};
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  REQUIRE_CALL(*mock_file, Write(kExpectedHeader)).TIMES(2).RETURN(true);
  REQUIRE_CALL(*mock_file, Write(kExpectedSize)).TIMES(2).RETURN(true);
  REQUIRE_CALL(*mock_file, Write(kExpectedContent)).TIMES(2).RETURN(true);

  // When
  REQUIRE(feature_storage.Write(kExpectedContent));
  REQUIRE(feature_storage.Write(kExpectedContent));
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M create new file if existing file is too old W Write",
                 "[feature_storage]") {
  using std::chrono::seconds;

  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  constexpr uint64_t fake_start_time = 123456789123L;
  auto mock_time_provider = [] {
    // DateTimeProvider will be called twice per file creation
    static int time_index = 0;
    constexpr auto ten_seconds = seconds(10);
    constexpr auto ten_seconds_ns =
        std::chrono::duration_cast<PerformancePreset::Nanoseconds>(ten_seconds);

    auto time = time_index < 2 ? fake_start_time
                               : (fake_start_time + ten_seconds_ns.count());
    time_index++;
    return time;
  };

  // Use original preset for sane defaults
  PerformancePreset performance_preset{
      performance_preset_.max_file_size(),
      performance_preset_.max_directory_size(),
      std::chrono::duration_cast<PerformancePreset::Nanoseconds>(seconds(5)),
      performance_preset_.min_file_age_for_read(),
      performance_preset_.initial_upload_delay(),
      performance_preset_.min_upload_delay(),
      performance_preset_.max_upload_delay(),
      performance_preset_.max_batches_per_upload()};
  FeatureStorage feature_storage{"TestFeature", performance_preset,
                                 mock_time_provider, file_system};
  ALLOW_CALL(*file_system, Exists(_)).RETURN(false);

  // Expect
  trompeloeil::sequence seq;
  auto mock_first_file = std::make_unique<MockDatadogFile>("any");
  REQUIRE_CALL(*file_system, Open(_))
      .IN_SEQUENCE(seq)
      .LR_RETURN(std::move(mock_first_file));
  REQUIRE_CALL(*mock_first_file, Write(_)).TIMES(1, 3).RETURN(true);
  auto mock_second_file = std::make_unique<MockDatadogFile>("any_2");
  REQUIRE_CALL(*mock_second_file, Write(_)).TIMES(1, 3).RETURN(true);
  REQUIRE_CALL(*file_system, Open(_))
      .IN_SEQUENCE(seq)
      .LR_RETURN(std::move(mock_second_file));

  // When
  REQUIRE(feature_storage.Write("First file contents"));
  REQUIRE(feature_storage.Write("Second file contents"));
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M create new file if existing file is too large W Write",
                 "[feature_storage]") {
  using std::chrono::seconds;

  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  PerformancePreset performance_preset{
      28,  // max file size in bytes
      performance_preset_.max_directory_size(),
      // Use original preset for sane defaults
      performance_preset_.max_file_age_for_write(),
      performance_preset_.min_file_age_for_read(),
      performance_preset_.initial_upload_delay(),
      performance_preset_.min_upload_delay(),
      performance_preset_.max_upload_delay(),
      performance_preset_.max_batches_per_upload()};

  FeatureStorage feature_storage{"TestFeature", performance_preset,
                                 DefaultDateTimeProvider, file_system};
  ALLOW_CALL(*file_system, Exists(_)).RETURN(false);

  // Expect
  trompeloeil::sequence seq;
  auto mock_first_file = std::make_unique<MockDatadogFile>("any");
  REQUIRE_CALL(*file_system, Open(_))
      .IN_SEQUENCE(seq)
      .LR_RETURN(std::move(mock_first_file));
  REQUIRE_CALL(*mock_first_file, Write(_)).TIMES(1, 3).RETURN(true);
  auto mock_second_file = std::make_unique<MockDatadogFile>("any_2");
  REQUIRE_CALL(*mock_second_file, Write(_)).TIMES(1, 3).RETURN(true);
  REQUIRE_CALL(*file_system, Open(_))
      .IN_SEQUENCE(seq)
      .LR_RETURN(std::move(mock_second_file));

  // When
  REQUIRE(feature_storage.Write("First file contents"));
  REQUIRE(feature_storage.Write("Second file contents"));
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M reject write if size is too large W Write",
                 "[feature_storage]") {
  using std::chrono::seconds;

  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  PerformancePreset performance_preset{
      16,  // max file size in bytes
      performance_preset_.max_directory_size(),
      // Use original preset for sane defaults
      performance_preset_.max_file_age_for_write(),
      performance_preset_.min_file_age_for_read(),
      performance_preset_.initial_upload_delay(),
      performance_preset_.min_upload_delay(),
      performance_preset_.max_upload_delay(),
      performance_preset_.max_batches_per_upload()};

  FeatureStorage feature_storage{"TestFeature", performance_preset,
                                 DefaultDateTimeProvider, file_system};
  ALLOW_CALL(*file_system, Exists(_)).RETURN(false);

  // When
  REQUIRE_FALSE(feature_storage.Write("First file contents"));
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M list no files when none exist",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};
  ALLOW_CALL(*file_system, ListFiles(_, _)).RETURN(true);

  // When
  std::vector<std::filesystem::path> readable_files;
  REQUIRE(feature_storage.ListReadableFiles(readable_files));

  // Then
  REQUIRE(readable_files.size() == 0);
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M list files W ListReadableFiles",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};
  ALLOW_CALL(*file_system, ListFiles(_, _))
      .LR_SIDE_EFFECT({
        _2.push_back("125566717");
        _2.push_back("1666712718");
      })
      .RETURN(true);

  // When
  std::vector<std::filesystem::path> readable_files;
  REQUIRE(feature_storage.ListReadableFiles(readable_files));

  // Then
  REQUIRE(readable_files.size() == 2);
  REQUIRE(readable_files[0] == "125566717");
  REQUIRE(readable_files[1] == "1666712718");
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M not list open file W ListReadableFiles",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  constexpr uint64_t fake_nanos = 123456789123L;
  auto mock_file_path =
      std::filesystem::path(std::to_string(NanoToMillis(fake_nanos)));
  auto mock_time_provider = [] { return fake_nanos; };
  auto mock_file = std::make_unique<MockDatadogFile>(mock_file_path);
  ALLOW_CALL(*mock_file, Write(_)).RETURN(true);
  ALLOW_CALL(*file_system, Open(mock_file_path))
      .LR_RETURN(std::move(mock_file));

  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 mock_time_provider, file_system};
  ALLOW_CALL(*file_system, ListFiles(_, _))
      .LR_SIDE_EFFECT({
        _2.push_back("125566717");
        _2.push_back(mock_file_path);
      })
      .RETURN(true);
  ALLOW_CALL(*file_system, Exists(_)).RETURN(false);
  // Write to open the file
  feature_storage.Write("File Contents");

  // When
  std::vector<std::filesystem::path> readable_files;
  REQUIRE(feature_storage.ListReadableFiles(readable_files));

  // Then
  REQUIRE(readable_files.size() == 1);
  REQUIRE(readable_files[0] == "125566717");
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M list files sorted numerically W ListReadableFiles",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  ALLOW_CALL(*file_system, ListFiles(_, _))
      .LR_SIDE_EFFECT({
        _2.push_back("1255667172");
        _2.push_back("125566717");
        _2.push_back("24412557");
        _2.push_back("125");
        _2.push_back("24412556");
        _2.push_back("1255667171");
      })
      .RETURN(true);
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};

  // When
  std::vector<std::filesystem::path> readable_files;
  REQUIRE(feature_storage.ListReadableFiles(readable_files));

  // Then
  REQUIRE(readable_files.size() == 6);
  REQUIRE(readable_files[0] == "125");
  REQUIRE(readable_files[1] == "24412556");
  REQUIRE(readable_files[2] == "24412557");
  REQUIRE(readable_files[3] == "125566717");
  REQUIRE(readable_files[4] == "1255667171");
  REQUIRE(readable_files[5] == "1255667172");
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M open file in file system W GetReadableFile",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  auto mock_file_path = std::filesystem::path("any");
  auto mock_file = std::make_unique<MockDatadogFile>(mock_file_path);
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};

  // Expect
  REQUIRE_CALL(*file_system, Open(mock_file_path))
      .LR_RETURN(std::move(mock_file));

  // When
  auto file = feature_storage.GetReadableFile(mock_file_path);

  // Then
  REQUIRE(file);
  REQUIRE(file->GetPath() == mock_file_path);
}

TEST_CASE_METHOD(FeatureStorageFixture,
                 "M delete file W DeleteReadableFile",
                 "[feature_storage]") {
  // Given
  auto file_system = std::make_shared<MockDatadogFileSystem>();
  auto mock_file_path = std::filesystem::path("any");
  auto mock_file = std::make_unique<MockDatadogFile>(mock_file_path);
  ALLOW_CALL(*file_system, Open(mock_file_path))
      .LR_RETURN(std::move(mock_file));
  FeatureStorage feature_storage{"TestFeature", performance_preset_,
                                 DefaultDateTimeProvider, file_system};

  // Expect
  REQUIRE_CALL(*file_system, Delete(mock_file_path))
      .RETURN(DatadogFileStatus::Ok);

  // When
  auto file = feature_storage.GetReadableFile(mock_file_path);
  REQUIRE(file);
  bool result = feature_storage.DeleteReadableFile(std::move(file));
  REQUIRE(result);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

}  // namespace
