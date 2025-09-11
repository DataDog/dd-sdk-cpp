#pragma once

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/types.h>
#endif

/**
 * Quick-and-dirty interface providing helper functions for platform-specific
 * functionality used by this benchmark program.
 */
namespace os {

struct ChildProcess {
#ifdef _WIN32
  DWORD pid;
  HANDLE handle;
#else
  pid_t pid;
#endif
};

/**
 * Returns the full path to the currently-running binary, or nullopt if unable.
 */
std::optional<std::filesystem::path> GetExecutablePath();

/**
 * Searches the PATH for python3 or python binaries, and returns the full path to the
 * first matching binary found. Returns nullopt if no matches.
 */
std::optional<std::filesystem::path> FindPythonExecutable();

/**
 * Starts a child process with the given set of command-line args. Returns nullopt if
 * unable to start the process.
 */
std::optional<ChildProcess> StartChildProcess(std::initializer_list<std::string> args);

/**
 * Terminates the given process, if it's still running. Returns once the process is no
 * longer running.
 */
void StopChildProcess(ChildProcess process);

}  // namespace os
