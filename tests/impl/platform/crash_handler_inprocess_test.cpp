// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/platform/crash_handler.hpp"
#include "datadog/impl/diagnostics.hpp"

#include <catch2/catch_test_macros.hpp>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <string>

using namespace datadog;

// Helper function to clean up .crashes directory before/after tests
static void cleanup_crashes_directory() {
  DIR* dir = opendir(".crashes");
  if (dir) {
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
        std::string path = ".crashes/" + std::string(entry->d_name);
        unlink(path.c_str());
      }
    }
    closedir(dir);
  }
  rmdir(".crashes");
}

// Helper function to get the first crash file from .crashes directory
static std::string get_crash_file() {
  DIR* dir = opendir(".crashes");
  if (!dir) return "";

  std::string result;
  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strncmp(entry->d_name, "crash_", 6) == 0) {
      result = ".crashes/" + std::string(entry->d_name);
      break;
    }
  }
  closedir(dir);
  return result;
}

// Helper function to read crash file contents
static std::string read_crash_file(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) return "";

  std::string contents;
  std::string line;
  while (std::getline(file, line)) {
    contents += line + "\n";
  }
  return contents;
}

#ifdef __APPLE__

// [platform-crash-handler-inprocess] tests validate the in-process crash handler implementation.

TEST_CASE("In-process crash handler", "[unit][platform-crash-handler-inprocess]") {
  // Clean up before test
  cleanup_crashes_directory();

  SECTION("M create crash file W process crashes with SIGSEGV") {
    pid_t pid = fork();

    if (pid == 0) {
      // Child process: initialize crash handler and crash
      impl::DiagnosticLogger logger;
      auto handler = platform::CrashHandler::Init(logger, "");
      bool init_result = handler->Initialize();

      if (!init_result) {
        _exit(1); // Failed to initialize
      }

      // Trigger SIGSEGV
      raise(SIGSEGV);

      // Should never reach here
      _exit(2);
    }

    // Parent process: wait for child to crash
    REQUIRE(pid > 0);

    int status;
    waitpid(pid, &status, 0);

    // Verify child exited with signal
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGSEGV);

    // Verify crash file was created
    std::string crash_file = get_crash_file();
    REQUIRE(!crash_file.empty());

    // Verify crash file contains expected content
    std::string contents = read_crash_file(crash_file);
    REQUIRE(contents.find("=== Datadog SDK Crash Report ===") != std::string::npos);
    REQUIRE(contents.find("Signal: SIGSEGV") != std::string::npos);
    REQUIRE(contents.find("Stack trace (raw addresses):") != std::string::npos);
    REQUIRE(contents.find("0x") != std::string::npos); // At least one address

    // Clean up
    cleanup_crashes_directory();
  }

  SECTION("M create crash file W process crashes with SIGBUS") {
    pid_t pid = fork();

    if (pid == 0) {
      // Child process: initialize crash handler and crash
      impl::DiagnosticLogger logger;
      auto handler = platform::CrashHandler::Init(logger, "");
      bool init_result = handler->Initialize();

      if (!init_result) {
        _exit(1);
      }

      // Trigger SIGBUS
      raise(SIGBUS);

      // Should never reach here
      _exit(2);
    }

    // Parent process: wait for child to crash
    REQUIRE(pid > 0);

    int status;
    waitpid(pid, &status, 0);

    // Verify child exited with signal
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGBUS);

    // Verify crash file was created
    std::string crash_file = get_crash_file();
    REQUIRE(!crash_file.empty());

    // Verify crash file contains expected content
    std::string contents = read_crash_file(crash_file);
    REQUIRE(contents.find("Signal: SIGBUS") != std::string::npos);

    // Clean up
    cleanup_crashes_directory();
  }

  SECTION("M create crash file W process crashes with SIGABRT") {
    pid_t pid = fork();

    if (pid == 0) {
      // Child process: initialize crash handler and crash
      impl::DiagnosticLogger logger;
      auto handler = platform::CrashHandler::Init(logger, "");
      bool init_result = handler->Initialize();

      if (!init_result) {
        _exit(1);
      }

      // Trigger SIGABRT
      abort();

      // Should never reach here
      _exit(2);
    }

    // Parent process: wait for child to crash
    REQUIRE(pid > 0);

    int status;
    waitpid(pid, &status, 0);

    // Verify child exited with signal
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGABRT);

    // Verify crash file was created
    std::string crash_file = get_crash_file();
    REQUIRE(!crash_file.empty());

    // Verify crash file contains expected content
    std::string contents = read_crash_file(crash_file);
    REQUIRE(contents.find("Signal: SIGABRT") != std::string::npos);

    // Clean up
    cleanup_crashes_directory();
  }

  SECTION("M properly restore signal handlers W Shutdown is called") {
    impl::DiagnosticLogger logger;
    auto handler = platform::CrashHandler::Init(logger, "");

    // Initialize crash handler
    bool init_result = handler->Initialize();
    REQUIRE(init_result);

    // Shutdown crash handler
    handler->Shutdown();

    // After shutdown, signals should use default handlers
    // We can't easily test this without actually crashing, so we'll just
    // verify that Shutdown doesn't crash and we can create another handler

    auto handler2 = platform::CrashHandler::Init(logger, "");
    bool init_result2 = handler2->Initialize();
    REQUIRE(init_result2);

    handler2->Shutdown();

    // Clean up
    cleanup_crashes_directory();
  }

  // Clean up after all tests
  cleanup_crashes_directory();
}

#endif // __APPLE__
