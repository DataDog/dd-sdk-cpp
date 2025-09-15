#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "assert.hpp"

/**
 * Returns the path to a suitable system-wide temp file directory.
 */
static std::filesystem::path _get_temp_root() {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  DWORD len = GetTempPathW(MAX_PATH, buffer);
  if (len > 0 && len < MAX_PATH) {
    return std::filesystem::path(buffer);
  }
  DATADOG_ASSERT(false, "Failed to get temp path");
  return std::filesystem::path("??*INVALID:PATH*??");
#else
  const char* tmpdir = std::getenv("TMPDIR");
  if (tmpdir) {
    return std::filesystem::path(tmpdir);
  } else {
    return std::filesystem::path("/tmp");
  }
#endif
}

/**
 * Generates a random string that is highly likely to be globally unique.
 */
static std::string _generate_random_string() {
  // Concatenate current timestamp and a random number
  auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << now << "-" << dist(gen);
  return oss.str();
}

/**
 * Returns the path to a directory that we should be able to create to serve as the
 * root storage path for a test.
 */
static std::filesystem::path _build_temp_dir_path() {
  const std::filesystem::path root = _get_temp_root();
  return root / "dd-native-test" / _generate_random_string();
}

/**
 * Wraps a temporary directory, for testing code that interfaces directly with the
 * filesystem.
 */
struct TempDirectory {
  // Full path to the directory that we exclusively manage
  std::string path;

  // Tests can override temporarily (or you can change this value here temporarily) in
  // order to examine files created on disk after the test (or just attach a debugger
  // and break before exit)
  bool should_delete{true};

  /**
   * Creates the directory a la `mkdir -p`.
   */
  TempDirectory() : path(_build_temp_dir_path().string()) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    DATADOG_ASSERT(ec == std::error_code{}, "failed to create temp directory");
  }

  /**
   * Deletes the directory upon leaving scope, unless should_delete is false.
   */
  ~TempDirectory() {
    if (should_delete && std::filesystem::exists(path)) {
      std::filesystem::remove_all(path);
    }
  }

  // Provide convenience functions for tests to examine the directory

  bool FileExists(std::string_view filename) const {
    std::filesystem::path file_path = std::filesystem::path(path) / filename;
    return std::filesystem::exists(file_path);
  }

  bool DirectoryExists(std::string_view dirname) const {
    std::filesystem::path dir_path = std::filesystem::path(path) / dirname;
    return std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path);
  }

  std::string ReadFileContents(std::string_view filename) const {
    std::filesystem::path file_path = std::filesystem::path(path) / filename;
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
      return "";
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
  }

  void WriteFile(std::string_view filename, std::string_view contents) const {
    std::filesystem::path file_path = std::filesystem::path(path) / filename;
    std::ofstream file(file_path, std::ios::binary);
    DATADOG_ASSERT(file.is_open(), "failed to open temp file for write");
    file.write(contents.data(), contents.size());
  }

  size_t GetFileSize(std::string_view filename) const {
    std::filesystem::path file_path = std::filesystem::path(path) / filename;
    std::error_code ec;
    auto size = std::filesystem::file_size(file_path, ec);
    return ec ? 0 : size;
  }
};
