// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include <datadog/storage/datadog_file_system.h>

#include <algorithm>
#include <fstream>

#include "test.h"

namespace {

using datadog::core::storage::IDatadogFile;
using datadog::core::storage::StdDatadogFileSystem;

// Creates a file on creation deletes the file on deletion
class TempFile {
 public:
  explicit TempFile(const std::filesystem::path& path) : path_(path) {
    // Create the file and immediately close it.
    std::fstream file{path, std::fstream::out | std::fstream::trunc};
    file.close();
  }

  ~TempFile() { std::filesystem::remove(path_); }

  const std::filesystem::path& GetPath() { return path_; }

 private:
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;

  const std::filesystem::path path_;
};

TEST_CASE("M return valid cache directory W GetBaseCacheDirectory",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;

  // When
  auto cache_dir = file_system.GetBaseDirectory();

  // Then
  REQUIRE(cache_dir == std::filesystem::path("caches/datadog"));
}

TEST_CASE("M create file in base cache directory W OpenFile", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  auto expected_path = std::filesystem::path{"caches/datadog/test.tmp"};

  // When
  { auto file = file_system.OpenFile("test.tmp"); }

  // Then
  REQUIRE(std::filesystem::exists(expected_path));

  // Cleanup
  std::filesystem::remove(expected_path);
}

TEST_CASE("M return nullptr W OpenFile fails", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  auto expected_path = std::filesystem::path{"noexist/test.tmp"};

  // When
  auto file = file_system.OpenFile("nexist/test.tmp");

  // Then
  REQUIRE_FALSE(file);
}

TEST_CASE("M return file empty list for empty directory W GetFiles",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  std::filesystem::path dir{"empty"};

  // When
  auto files = file_system.GetFiles(dir);

  // Then
  REQUIRE(files.empty());
}

TEST_CASE("M return file names for directory W GetFiles", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile file1(file_system_dir / "file1.tmp");
  TempFile file2(file_system_dir / "file2.tmp");

  // When
  auto files = file_system.GetFiles("");

  // Then
  REQUIRE(std::find(files.begin(), files.end(),
                    file_system.GetBaseDirectory() / "file1.tmp") !=
          files.end());
  REQUIRE(std::find(files.begin(), files.end(),
                    file_system.GetBaseDirectory() / "file2.tmp") !=
          files.end());
}

TEST_CASE("M not recurse directories W GetFiles", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  std::filesystem::create_directories(file_system_dir / "subdir");
  TempFile file1(file_system_dir / "subdir" / "file1.tmp");

  // When
  auto files = file_system.GetFiles("");

  // Then
  REQUIRE(std::find(files.begin(), files.end(),
                    file_system.GetBaseDirectory() / "subdir" / "file1.tmp") ==
          files.end());
}

TEST_CASE("M delete file W DeleteFile", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile file1(file_system_dir / "file1.tmp");

  // When
  file_system.DeleteFile("file1.tmp");

  // Then
  REQUIRE(!std::filesystem::exists(file1.GetPath()));
}

TEST_CASE("M fail silently W DeleteFile {nonexistant file}", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();

  // When / Then
  REQUIRE_NOTHROW(file_system.DeleteFile("noexist.tmp"));
}

TEST_CASE("M write file content W IDatadogFileFile::Write", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile file1(file_system_dir / "write_file.tmp");
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
  const char file_content[] = {"file contents\n"};

  // When
  {
    std::unique_ptr<IDatadogFile> file = file_system.OpenFile("write_file.tmp");
    REQUIRE(file);
    file->Write(static_cast<const char*>(file_content), sizeof(file_content));
  }

  // Then
  std::fstream file{file1.GetPath(), std::fstream::in};
  std::string actual_file_contents;
  std::getline(file, actual_file_contents);

  REQUIRE(actual_file_contents == "file contents");
}

}  // namespace
