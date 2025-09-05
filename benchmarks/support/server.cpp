#include "support/server.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <tlhelp32.h>
#include <windows.h>
typedef DWORD server_pid_t;
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
typedef pid_t server_pid_t;
#endif

// Embedded contents of server.py, included in the build via CMake
#include "embedded_server_py.h"

std::string write_temp_server_py() {
  std::error_code ec;
  auto temp_dir = std::filesystem::temp_directory_path(ec);
  if (ec != std::errc{}) {
    std::cerr << "failed to resolve temp directory path\n";
    std::exit(1);  // NOLINT(concurrency-mt-unsafe)
  }

  auto temp_path = temp_dir / "server.py";
  std::ofstream outfile(temp_path, std::ios::binary);
  if (!outfile.is_open()) {
    std::cerr << "failed to open temp file for write: " << temp_path << "\n";
    std::exit(1);  // NOLINT(concurrency-mt-unsafe)
  }
  outfile.write(
      reinterpret_cast<const char*>(server_py_data),  // NOLINT
      static_cast<std::streamsize>(server_py_size)
  );
  if (outfile.fail() || outfile.bad()) {
    std::cerr << "failed to write to temp file: " << temp_path << "\n";
    std::exit(1);  // NOLINT(concurrency-mt-unsafe)
  }
  return temp_path.string();
}

std::string find_python_executable() {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#ifdef _WIN32
  const char* candidates[] = {"py", "python", "python3", nullptr};
#else
  const char* candidates[] = {"python3", "python", nullptr};
#endif

  for (const char** candidate = candidates; *candidate != nullptr; ++candidate) {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD result = SearchPathA(nullptr, *candidate, ".exe", MAX_PATH, path, nullptr);
    if (result > 0 && result < MAX_PATH) {
      return std::string(path);
    }
#else
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
          return result;
        }
      } else {
        pclose(pipe);
      }
    }
#endif
  }
  return "";
  // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

// Process tracking for our python child process
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static server_pid_t s_server_pid = 0;
#ifdef _WIN32
static HANDLE s_server_process_handle = INVALID_HANDLE_VALUE;
#endif
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::string s_temp_server_path;

extern "C" {

void server_start(const server_opts_t* opts) {
  if (s_server_pid != 0) {
    std::cerr << "Server already running with PID " << s_server_pid << "\n";
    return;
  }

  s_temp_server_path = write_temp_server_py();
  const std::string python_exe = find_python_executable();
  if (python_exe.empty()) {
    std::cerr << "Python executable not found in PATH\n";
    std::exit(1);  // NOLINT(concurrency-mt-unsafe)
  }

  const std::string port = std::to_string(opts->port);
  const std::string response_delay_ms = std::to_string(opts->response_delay_ms);
  const std::string response_delay_variability_ms =
      std::to_string(opts->response_delay_variability_ms);

#ifdef _WIN32
  // Build command line for Windows
  std::string cmdline = "\"" + python_exe + "\" \"" + s_temp_server_path + "\"";
  cmdline += " --port " + port;
  cmdline += " --response-delay-ms " + response_delay_ms;
  cmdline += " --response-delay-variability-ms " + response_delay_variability_ms;

  STARTUPINFOA si = {};
  PROCESS_INFORMATION pi = {};
  si.cb = sizeof(si);

  // Create detached process
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
    std::cerr << "Failed to start server process: " << GetLastError() << "\n";
    std::exit(1);  // NOLINT(concurrency-mt-unsafe)
  }

  s_server_pid = pi.dwProcessId;
  s_server_process_handle = pi.hProcess;
  CloseHandle(pi.hThread);  // We don't need the thread handle

#else
  // POSIX: Use posix_spawn
  const char* argv[] = {
      python_exe.c_str(),
      s_temp_server_path.c_str(),
      "--port",
      port.c_str(),
      "--response-delay-ms",
      response_delay_ms.c_str(),
      "--response-delay-variability-ms",
      response_delay_variability_ms.c_str(),
      nullptr
  };

  pid_t pid = 0;
  int result = posix_spawn(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
      &pid, python_exe.c_str(), nullptr, nullptr, const_cast<char* const*>(argv),
      environ
  );

  if (result != 0) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    std::cerr << "Failed to start server process: " << strerror(result) << "\n";
    std::exit(1);  // NOLINT(concurrency-mt-unsafe)
  }

  s_server_pid = pid;
#endif

  std::cout << "Started Python server with PID " << s_server_pid << "\n";
}

void server_stop(void) {
  if (s_server_pid == 0) {
    return;  // No server running
  }

  std::cout << "Stopping server with PID " << s_server_pid << "\n";

#ifdef _WIN32
  if (s_server_process_handle != INVALID_HANDLE_VALUE) {
    // Try to terminate the process gracefully first
    if (TerminateProcess(s_server_process_handle, 0)) {
      // Wait for process to exit (up to 5 seconds)
      DWORD wait_result = WaitForSingleObject(s_server_process_handle, 5000);
      if (wait_result == WAIT_TIMEOUT) {
        std::cerr << "Server process did not exit gracefully\n";
      }
    } else {
      std::cerr << "Failed to terminate server process: " << GetLastError() << "\n";
    }

    CloseHandle(s_server_process_handle);
    s_server_process_handle = INVALID_HANDLE_VALUE;
  }

#else
  // POSIX: Send SIGTERM first, then SIGKILL if necessary
  if (kill(s_server_pid, SIGTERM) == 0) {
    // Wait up to 5 seconds for graceful termination
    for (int i = 0; i < 50; ++i) {
      int status = -1;
      pid_t result = waitpid(s_server_pid, &status, WNOHANG);
      if (result == s_server_pid) {
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
    if (kill(s_server_pid, 0) == 0) {
      // Process still alive, force kill
      std::cerr << "Server didn't respond to SIGTERM, sending SIGKILL\n";
      kill(s_server_pid, SIGKILL);

      // Wait for forced termination
      int status = -1;
      waitpid(s_server_pid, &status, 0);
    }
  } else {
    // kill() failed, process might already be dead
    std::cerr << "Failed to send signal to server process (might already be dead)\n";
  }
#endif

  s_server_pid = 0;

  // Clean up temp file
  if (!s_temp_server_path.empty()) {
    std::error_code ec;
    std::filesystem::remove(s_temp_server_path, ec);
    if (ec) {
      std::cerr << "Failed to remove temp server file: " << s_temp_server_path << "\n";
    }
    s_temp_server_path.clear();
  }

  std::cout << "Server stopped\n";
}
}
