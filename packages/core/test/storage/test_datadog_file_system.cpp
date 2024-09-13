// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include <datadog/storage/datadog_file_system.h>

#include <algorithm>
#include <fstream>

#include "test.h"

namespace {

using datadog::core::storage::DatadogFileStatus;
using datadog::core::storage::DatadogReadBuffer;
using datadog::core::storage::IDatadogFile;
using datadog::core::storage::StdDatadogFileSystem;

enum class FileDisposition : bool { remove_only, create_and_remove };

// Creates a file on construction deletes the file on destruction
class TempFile {
 public:
  explicit TempFile(
      const std::filesystem::path& path,
      FileDisposition file_disposition = FileDisposition::create_and_remove)
      : path_(path) {
    // Create the file and immediately close it.
    if (file_disposition == FileDisposition::create_and_remove) {
      std::fstream{path, std::fstream::out | std::fstream::trunc};
    }
  }

  ~TempFile() { std::filesystem::remove(path_); }

  const std::filesystem::path& GetPath() { return path_; }

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;

 private:
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
  TempFile cleanup{expected_path, FileDisposition::remove_only};

  // When
  { auto file = file_system.OpenFile("test.tmp"); }

  // Then
  REQUIRE(std::filesystem::exists(expected_path));
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

TEST_CASE("M not create file outside of its directory W OpenFile {relative}",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile cleanup{"caches/test.tmp", FileDisposition::remove_only};

  // When
  {
    auto file = file_system.OpenFile("../test.tmp");
    REQUIRE(file == nullptr);
  }

  // Then
  REQUIRE(!std::filesystem::exists("caches/test.tmp"));
}

TEST_CASE("M not create file outside of its directory W OpenFile {rooted}",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile cleanup{"/tmp/test.tmp", FileDisposition::remove_only};

  // When
  {
    auto file = file_system.OpenFile("/tmp/test.tmp");
    REQUIRE(file == nullptr);
  }

  // Then
  REQUIRE(!std::filesystem::exists("/tmp/test.tmp"));
}

TEST_CASE("M return empty list for empty directory W GetFiles", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  std::filesystem::path dir{"empty"};

  // When
  auto files = file_system.GetFiles(dir);

  // Then
  REQUIRE(files.empty());
}

TEST_CASE(
    "M return empty list for directory outside the filesystem W GetFiles "
    "{relative}",
    "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile file1(file_system.GetBaseDirectory() / "../file1.tmp");
  std::filesystem::path dir{"../"};

  // When
  auto files = file_system.GetFiles(dir);

  // Then
  REQUIRE(files.empty());
}

TEST_CASE(
    "M return empty list for directory outside the filesystem W GetFiles "
    "{rooted}",
    "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  std::filesystem::path dir("/usr/bin");

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
  REQUIRE(file_system.DeleteFile("file1.tmp"));

  // Then
  REQUIRE(!std::filesystem::exists(file1.GetPath()));
}

TEST_CASE("M fail silently W DeleteFile {nonexistant file}", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();

  // When
  bool result = false;
  REQUIRE_NOTHROW(result = file_system.DeleteFile("noexist.tmp"));

  // Then
  REQUIRE_FALSE(result);
}

TEST_CASE(
    "M not delete files outside of file system W DeleteFile {relative file}",
    "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile file1("caches/file1.tmp");

  // When
  bool result = false;
  REQUIRE_NOTHROW(result = file_system.DeleteFile("../file1.tmp"));

  // Then
  REQUIRE_FALSE(result);
  REQUIRE(std::filesystem::exists("caches/file1.tmp"));
}

TEST_CASE("M not delete files outside of file system W DeleteFile {rooted}",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile file1("/tmp/file1.tmp");

  // When
  bool result = false;
  REQUIRE_NOTHROW(file_system.DeleteFile("/tmp/file1.tmp"));

  REQUIRE_FALSE(result);
  REQUIRE(std::filesystem::exists("/tmp/file1.tmp"));
}

TEST_CASE("M write file content W IDatadogFileFile::Write", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile file1(file_system_dir / "write_file.tmp");
  std::string file_content{"file contents\n"};

  // When
  {
    std::unique_ptr<IDatadogFile> file = file_system.OpenFile("write_file.tmp");
    REQUIRE(file);
    auto status = file->Write(file_content);
    REQUIRE(status == DatadogFileStatus::Ok);
  }

  // Then
  std::fstream file{file1.GetPath(), std::fstream::in};
  std::string actual_file_contents;
  std::getline(file, actual_file_contents);

  REQUIRE(actual_file_contents == "file contents");
}

TEST_CASE("M read file content W IDatadogFileFile::Read", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile temp_file(file_system_dir / "read_file.tmp");

  {
    std::fstream write_file{file_system_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  constexpr int buffer_size = 128;
  DatadogReadBuffer buffer(buffer_size);
  {
    std::unique_ptr<IDatadogFile> file = file_system.OpenFile("read_file.tmp");
    REQUIRE(file);
    auto status = file->Read(buffer);
    REQUIRE(status == DatadogFileStatus::EndOfFile);
  }

  // Then
  REQUIRE(std::string_view(buffer.data(), buffer.size()) == "file contents\n");
}

TEST_CASE("M partial file content W IDatadogFileFile::Read", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile temp_file(file_system_dir / "read_file.tmp");

  {
    std::fstream write_file{file_system_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  constexpr int buffer_size = 8;
  DatadogReadBuffer buffer(buffer_size);
  {
    std::unique_ptr<IDatadogFile> file = file_system.OpenFile("read_file.tmp");
    REQUIRE(file);
    auto status = file->Read(buffer);
    REQUIRE(status == DatadogFileStatus::Ok);
  }

  // Then
  REQUIRE(buffer.size() == buffer_size);
  REQUIRE(std::string_view(buffer.data(), buffer.size()) == "file con");
}

TEST_CASE("M file contents looped W IDatadogFileFile::Read", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  const auto& file_system_dir = file_system.GetBaseDirectory();
  TempFile temp_file(file_system_dir / "read_file.tmp");

  {
    std::fstream write_file{file_system_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  constexpr int buffer_size = 8;
  DatadogReadBuffer buffer(buffer_size);
  {
    std::unique_ptr<IDatadogFile> file = file_system.OpenFile("read_file.tmp");
    REQUIRE(file);
    while (file->Read(buffer) == DatadogFileStatus::Ok &&
           buffer.size() == buffer_size) {
    }
  }
}

}  // namespace
