// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "datadog/impl/platform/filesystem.hpp"

#include "mock/filesystem.hpp"

using namespace datadog;

// [mock-filesystem] tests validate that the mock filesystem implementation behaves
// correctly and consistently

TEST_CASE("MockFilesystem Basic Operations", "[unit][mock-filesystem]") {
  MockStorageDirectory storage;

  SECTION("M create valid mock storage directory W default constructor") {
    REQUIRE_NOTHROW(MockStorageDirectory{});
  }

  SECTION("M list empty directory W no files exist") {
    std::vector<std::string> files;
    auto result = storage.ListFiles(files);

    REQUIRE(result.has_value());
    REQUIRE(files.empty());
  }

  SECTION("M list files W files exist") {
    // Given files exist in the directory
    storage.WithExistingFile("test1.dat", "content1");
    storage.WithExistingFile("test2.txt", "content2");

    // When listing files
    std::vector<std::string> files;
    auto result = storage.ListFiles(files);

    // Then operation succeeds and contains expected files
    REQUIRE(result.has_value());
    REQUIRE(files.size() == 2);
    REQUIRE(std::find(files.begin(), files.end(), "test1.dat") != files.end());
    REQUIRE(std::find(files.begin(), files.end(), "test2.txt") != files.end());
  }

  SECTION("M delete existing file W file exists") {
    // Given a file exists
    storage.WithExistingFile("to_delete.txt", "delete me");

    // When deleting the file
    auto result = storage.RemoveFile("to_delete.txt");

    // Then operation succeeds and file is gone from listing
    REQUIRE(result.has_value());
    std::vector<std::string> files;
    storage.ListFiles(files);
    REQUIRE(files.empty());
  }

  SECTION("M return DoesNotExist W delete non-existent file") {
    // When deleting non-existent file
    auto result = storage.RemoveFile("nonexistent.txt");

    // Then operation fails with DoesNotExist error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == platform::FilesystemError::DoesNotExist);
  }
}

TEST_CASE("MockFilesystem File Reading", "[unit][mock-filesystem]") {
  MockStorageDirectory storage;

  SECTION("M return DoesNotExist W open non-existent file") {
    // When opening non-existent file for read
    auto result = storage.OpenForRead("nonexistent.txt");

    // Then operation fails with DoesNotExist error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == platform::FilesystemError::DoesNotExist);
  }

  SECTION("M successfully read file W file exists") {
    // Given a file with known content
    const std::string test_content = "Hello, Mock World!";
    storage.WithExistingFile("readable.txt", test_content);

    // When opening file for read and reading content
    auto reader_result = storage.OpenForRead("readable.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    char buffer[100];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));

    // Then operation succeeds and reads expected content
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == test_content.size());
    REQUIRE(std::memcmp(buffer, test_content.data(), test_content.size()) == 0);
  }

  SECTION("M handle partial reads W small buffer") {
    // Given a file with content larger than buffer
    const std::string test_content = "This is a longer test content";
    storage.WithExistingFile("partial.txt", test_content);

    // When opening and reading with small buffer
    auto reader_result = storage.OpenForRead("partial.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    char buffer[10];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));

    // Then operation succeeds and reads partial content
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == sizeof(buffer));
    REQUIRE(std::memcmp(buffer, test_content.data(), sizeof(buffer)) == 0);
  }

  SECTION("M handle EOF correctly W read empty file") {
    // Given an empty file
    storage.WithExistingFile("empty.txt", "");

    // When opening and reading the empty file
    auto reader_result = storage.OpenForRead("empty.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    char buffer[10];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));

    // Then operation succeeds with zero bytes read
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == 0);
  }

  SECTION("M seek forward and read W valid offset") {
    // Given a file with known content
    const std::string test_content = "0123456789abcdef";
    storage.WithExistingFile("seekable.txt", test_content);

    // When opening file and seeking forward
    auto reader_result = storage.OpenForRead("seekable.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    auto seek_result = reader->Seek(5);
    REQUIRE(seek_result.has_value());

    // Then subsequent read starts from seeked position
    char buffer[5];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == sizeof(buffer));
    REQUIRE(std::memcmp(buffer, "56789", sizeof(buffer)) == 0);
  }

  SECTION("M seek backward W negative offset after forward seek") {
    // Given a file with known content
    const std::string test_content = "0123456789abcdef";
    storage.WithExistingFile("seekback.txt", test_content);

    auto reader_result = storage.OpenForRead("seekback.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    auto seek_forward = reader->Seek(8);
    REQUIRE(seek_forward.has_value());

    // When seeking backward
    auto seek_back = reader->Seek(-3);
    REQUIRE(seek_back.has_value());

    // Then subsequent read starts from new position
    char buffer[3];
    auto read_result = reader->Read(buffer, sizeof(buffer));
    REQUIRE(read_result.has_value());
    REQUIRE(std::memcmp(buffer, "567", sizeof(buffer)) == 0);
  }

  SECTION("M return Failed W seek before beginning") {
    // Given a file with content
    storage.WithExistingFile("seek_error.txt", "content");

    auto reader_result = storage.OpenForRead("seek_error.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    // When seeking before beginning of file
    auto seek_result = reader->Seek(-10);

    // Then operation fails with Failed error
    REQUIRE_FALSE(seek_result.has_value());
    REQUIRE(seek_result.error() == platform::FilesystemError::Failed);
  }

  SECTION("M handle seek past EOF W offset beyond file end") {
    // Given a short file
    const std::string test_content = "short";
    storage.WithExistingFile("seek_eof.txt", test_content);

    auto reader_result = storage.OpenForRead("seek_eof.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    // When seeking past end of file
    auto seek_result = reader->Seek(100);
    REQUIRE(seek_result.has_value());  // Should not error

    // Then subsequent reads do nothing and return 0 bytes read
    char buffer[10];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == 0);
  }
}

TEST_CASE("MockFilesystem Exclusive Locking", "[unit][mock-filesystem]") {
  MockStorageDirectory storage;

  SECTION("M prevent concurrent readers W file already open for read") {
    // Given a file exists
    storage.WithExistingFile("exclusive.txt", "test data");

    // When first reader opens successfully
    auto reader1_result = storage.OpenForRead("exclusive.txt");
    REQUIRE(reader1_result.has_value());
    auto reader1 = std::move(reader1_result.value());

    // Then second reader should fail
    auto reader2_result = storage.OpenForRead("exclusive.txt");
    REQUIRE_FALSE(reader2_result.has_value());
    REQUIRE(reader2_result.error() == platform::FilesystemError::Failed);

    // And after first reader is destroyed, second attempt should succeed
    reader1.reset();
    auto reader3_result = storage.OpenForRead("exclusive.txt");
    REQUIRE(reader3_result.has_value());
  }
}

TEST_CASE("MockFilesystem Error Simulation", "[unit][mock-filesystem]") {
  MockStorageDirectory storage;

  SECTION("M simulate I/O error W file marked as bad") {
    // Given a file that's been preemptively marked corrupt
    storage.WithExistingFile("bad_file.txt", "test data");
    storage.Corrupt("bad_file.txt");

    // When we attempt to open the file
    auto reader_result = storage.OpenForRead("bad_file.txt");

    // Then we should get an IOError
    REQUIRE(!reader_result.has_value());
    REQUIRE(reader_result.error() == platform::FilesystemError::IOError);
  }

  SECTION("M simulate I/O error W bad directory") {
    // Given a directory is marked as corrupted
    storage.WithExistingFile("subdir/file.txt", "test data");
    storage.Corrupt("subdir");

    // When accessing the corrupted directory
    auto subdir_result = storage.PrepareSubdirectory("subdir");
    REQUIRE(subdir_result.has_value());
    auto subdir = std::move(subdir_result.value());

    std::vector<std::string> files;
    auto list_result = subdir->ListFiles(files);

    // Then directory operations should fail with I/O error
    REQUIRE_FALSE(list_result.has_value());
    REQUIRE(list_result.error() == platform::FilesystemError::IOError);
  }
}

TEST_CASE("MockFilesystem Subdirectories", "[unit][mock-filesystem]") {
  MockStorageDirectory storage;

  SECTION("M create subdirectory W PrepareSubdirectory") {
    auto subdir_result = storage.PrepareSubdirectory("testdir");

    REQUIRE(subdir_result.has_value());
    REQUIRE(subdir_result.value() != nullptr);
  }

  SECTION("M list files in subdirectory W ListFiles on subdirectory") {
    // Given files exist in subdirectory
    auto subdir_result = storage.PrepareSubdirectory("listdir");
    REQUIRE(subdir_result.has_value());
    auto subdir = std::move(subdir_result.value());

    storage.WithExistingFile("listdir/file1.dat", "content1");
    storage.WithExistingFile("listdir/file2.txt", "content2");

    // When listing files in subdirectory
    std::vector<std::string> files;
    auto list_result = subdir->ListFiles(files);

    // Then operation succeeds and contains expected files
    REQUIRE(list_result.has_value());
    REQUIRE(files.size() == 2);
    REQUIRE(std::find(files.begin(), files.end(), "file1.dat") != files.end());
    REQUIRE(std::find(files.begin(), files.end(), "file2.txt") != files.end());
  }

  SECTION("M create nested subdirectories W recursive PrepareSubdirectory") {
    // When creating nested subdirectory handle
    auto subdir_result = storage.PrepareSubdirectory("parent");
    REQUIRE(subdir_result.has_value());
    auto subdir = std::move(subdir_result.value());

    auto nested_result = subdir->PrepareSubdirectory("child");
    REQUIRE(nested_result.has_value());
    auto nested = std::move(nested_result.value());

    // And populating it with a file via WithExistingFile
    storage.WithExistingFile("parent/child/deep_file.txt", "Deep nested content");

    // Then the file is accessible through the nested IDirectory handle
    auto reader_result = nested->OpenForRead("deep_file.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    char buffer[50];
    auto read_result = reader->Read(buffer, sizeof(buffer));
    REQUIRE(read_result.has_value());
    const char* expected = "Deep nested content";
    REQUIRE(std::memcmp(buffer, expected, std::strlen(expected)) == 0);
  }
}

TEST_CASE("MockFilesystem Threading Safety", "[unit][mock-filesystem]") {
  MockStorageDirectory storage;

  SECTION("M handle concurrent different file operations W multiple threads") {
    // Given multiple files for concurrent access
    storage.WithExistingFile("file1.txt", "content1");
    storage.WithExistingFile("file2.txt", "content2");

    std::vector<std::thread> threads;
    std::vector<int> thread_results(2, false);

    // Thread 1: Read file1
    threads.emplace_back([&storage, &thread_results]() {
      auto reader = storage.OpenForRead("file1.txt");
      if (reader.has_value()) {
        char buffer[10];
        auto result = reader.value()->Read(buffer, sizeof(buffer));
        thread_results[0] = result.has_value() ? 1 : 0;
      }
    });

    // Thread 2: List files
    threads.emplace_back([&storage, &thread_results]() {
      std::vector<std::string> files;
      auto result = storage.ListFiles(files);
      thread_results[1] = (result.has_value() && files.size() >= 2) ? 1 : 0;
    });

    // Wait for all threads to complete
    for (auto& t : threads) {
      t.join();
    }

    // Then all operations should succeed
    REQUIRE(thread_results[0] == 1);
    REQUIRE(thread_results[1] == 1);
  }

  SECTION("M enforce exclusivity W concurrent access to same file") {
    // Given a file exists and multiple threads try to access it
    storage.WithExistingFile("contested.txt", "original content");

    std::vector<std::thread> threads;
    std::atomic<int> successful_opens{0};
    std::atomic<int> failed_opens{0};

    // When starting 5 threads trying to open the same file
    for (int i = 0; i < 5; ++i) {
      threads.emplace_back([&storage, &successful_opens, &failed_opens]() {
        auto reader = storage.OpenForRead("contested.txt");
        if (reader.has_value()) {
          successful_opens++;
          // Hold the file briefly to increase contention
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
          failed_opens++;
        }
      });
    }

    // Wait for all threads
    for (auto& t : threads) {
      t.join();
    }

    // Then exactly one should succeed due to exclusive locking
    REQUIRE(successful_opens == 1);
    REQUIRE(failed_opens == 4);
  }
}
