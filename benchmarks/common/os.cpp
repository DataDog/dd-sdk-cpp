#include "common/os.hpp"

#include <iostream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/syslimits.h>
extern char** environ;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
#endif
#endif

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

std::optional<std::filesystem::path> os::GetExecutablePath() {
#ifdef _WIN32
  // Windows: Call GetModuleFileName
  char exe_path[MAX_PATH];
  DWORD result = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
  if (result == 0 || result >= MAX_PATH) {
    return std::nullopt;
  }
  return std::filesystem::path(exe_path);
#elif __APPLE__
  // Darwin: Call _NSGetExecutablePath + realpath
  char exe_path[PATH_MAX];
  uint32_t size = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &size) != 0) {
    return std::nullopt;
  }
  char resolved_path[PATH_MAX];
  if (realpath(exe_path, resolved_path) == nullptr) {
    return std::nullopt;
  }
  return std::filesystem::path(resolved_path);
#else
  // Linux: Read /proc/self/exe
  char exe_path[4096];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len == -1) {
    return std::nullopt;
  }
  exe_path[len] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
  return std::filesystem::path(exe_path);
#endif
}

std::optional<std::filesystem::path> os::FindPythonExecutable() {
  // Use system API to ask for the path to either a python3 or python binary
  std::string found_path{};
  const char* candidates[] = {"python3", "python", nullptr};
  for (const char** candidate = candidates; *candidate != nullptr; ++candidate) {
#ifdef _WIN32
    // Windows: Call SearchPath
    char path[MAX_PATH];
    DWORD result = SearchPathA(nullptr, *candidate, ".exe", MAX_PATH, path, nullptr);
    if (result > 0 && result < MAX_PATH) {
      found_path = std::string(path);
      break;
    }
#else
    // POSIX: Execute 'which python' etc. with popen
    std::string cmd = "which ";
    cmd += *candidate;
    cmd += " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      char buffer[256];
      if (fgets(buffer, sizeof(buffer), pipe)) {
        pclose(pipe);
        std::string result(buffer);
        if (!result.empty() && result.back() == '\n') {
          result.pop_back();
        }
        if (!result.empty()) {
          found_path = result;
          break;
        }
      } else {
        pclose(pipe);
      }
    }
#endif
  }

  // If nothing found, return nullopt; otherwise return the value as a filesystem::path
  if (found_path.empty()) {
    return std::nullopt;
  }
  return std::filesystem::path(found_path);
}

std::optional<os::ChildProcess> os::StartChildProcess(
    std::initializer_list<std::string> args
) {
  if (args.size() == 0) {
    return std::nullopt;
  }

#ifdef _WIN32
  // Windows: build command-line string, then call CreateProcess
  std::string cmdline;
  bool first = true;
  for (const auto& arg : args) {
    if (!first) {
      cmdline += " ";
    }
    // Quote arguments that contain spaces
    if (arg.find(' ') != std::string::npos) {
      cmdline += "\"" + arg + "\"";
    } else {
      cmdline += arg;
    }
    first = false;
  }

  STARTUPINFOA si = {};
  PROCESS_INFORMATION pi = {};
  si.cb = sizeof(si);
  BOOL success = CreateProcessA(
      nullptr,           // Application name
      cmdline.data(),    // Command line
      nullptr,           // Process security attributes
      nullptr,           // Thread security attributes
      FALSE,             // Inherit handles
      DETACHED_PROCESS,  // Creation flags
      nullptr,           // Environment
      nullptr,           // Current directory
      &si,               // Startup info
      &pi                // Process info
  );
  if (!success) {
    return std::nullopt;
  }

  CloseHandle(pi.hThread);  // We don't need the thread handle
  return ChildProcess{pi.dwProcessId, pi.hProcess};
#else
  // POSIX: Convert args to char**, then call posix_spawn
  std::vector<const char*> argv;
  for (const auto& arg : args) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);

  pid_t pid = 0;
  int result = posix_spawn(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
      &pid, argv[0], nullptr, nullptr, const_cast<char* const*>(argv.data()), environ
  );
  if (result != 0) {
    return std::nullopt;
  }
  return ChildProcess{pid};
#endif
}

void os::StopChildProcess(os::ChildProcess process) {
#ifdef _WIN32
  // Windows: Call TerminateProcess, then eventually CloseHandle
  if (process.handle != INVALID_HANDLE_VALUE) {
    // Try to terminate the process gracefully first
    if (TerminateProcess(process.handle, 0)) {
      // Wait for process to exit (up to 5 seconds)
      DWORD wait_result = WaitForSingleObject(process.handle, 5000);
      if (wait_result == WAIT_TIMEOUT) {
        std::cerr << "Server process did not exit gracefully\n";
      }
    } else {
      std::cerr << "Failed to terminate server process: " << GetLastError() << "\n";
    }
    CloseHandle(process.handle);
  }
#else
  // POSIX: Send SIGTERM first, then SIGKILL if necessary
  if (kill(process.pid, SIGTERM) == 0) {
    // Wait up to 5 seconds for graceful termination
    for (int i = 0; i < 50; ++i) {
      int status = -1;
      pid_t result = waitpid(process.pid, &status, WNOHANG);
      if (result == process.pid) {
        // Process has exited
        break;
      }
      if (result == -1) {
        // Process doesn't exist or other error
        break;
      }
      usleep(100000);  // Sleep 100ms
    }

    // Check if process is still running
    if (kill(process.pid, 0) == 0) {
      // Process still alive, force kill
      std::cerr << "Server didn't respond to SIGTERM, sending SIGKILL\n";
      kill(process.pid, SIGKILL);

      // Wait for forced termination
      int status = -1;
      waitpid(process.pid, &status, 0);
    }
  } else {
    // kill() failed, process might already be dead
    std::cerr << "Failed to send signal to server process (might already be dead)\n";
  }
#endif
}

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
