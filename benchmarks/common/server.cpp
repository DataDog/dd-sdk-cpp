#include "common/server.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "common/exit.hpp"
#include "common/os.hpp"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::optional<os::ChildProcess> s_server_process;

void StartServer(const ServerOptions& opts) {
  // Make sure we're not already running the server; ignore this call if so
  if (s_server_process) {
    std::cerr << "Server already running with PID " << s_server_process->pid << "\n";
    return;
  }

  // Find a usable Python interpreter
  auto python_exe_opt = os::FindPythonExecutable();
  if (!python_exe_opt) {
    std::cerr << "failed to find python/python3 executable\n";
    Exit(1);
    return;
  }
  const std::string& python_exe = *python_exe_opt;

  // Get the path of the current executable
  auto exe_path_opt = os::GetExecutablePath();
  if (!exe_path_opt) {
    std::cerr << "failed to get executable path\n";
    Exit(1);
    return;
  }

  // Check to see if server.py exists in the same directory
  auto exe_dir = exe_path_opt->parent_path();
  auto server_py_path = exe_dir / "server.py";
  if (!std::filesystem::exists(server_py_path)) {
    std::cerr << "server.py not found in executable directory: " << server_py_path
              << "\n";
    Exit(1);
  }

  // Prepare command-line options to pass to server.py
  const std::string port = std::to_string(opts.port);
  const std::string response_delay_ms = std::to_string(opts.response_delay_ms);
  const std::string response_delay_variability_ms =
      std::to_string(opts.response_delay_variability_ms);

  // Attempt to spawn a child process that will run server.py in a Python interpreter
  auto process_opt = os::StartChildProcess(
      {python_exe, server_py_path, "--port", port, "--response-delay-ms",
       response_delay_ms, "--response-delay-variability-ms",
       response_delay_variability_ms}
  );
  if (!process_opt) {
    std::cerr << "Failed to start Python server process\n";
    Exit(1);
    return;
  }
  s_server_process = *process_opt;
  std::cout << "Started Python server with PID " << s_server_process->pid << "\n";
}

void StopServer() {
  // If the server isn't running, ignore this call
  if (!s_server_process) {
    return;
  }

  // Send a polite signal and wait for shutdown, killing after timeout
  std::cout << "Stopping server with PID " << s_server_process->pid << "\n";
  os::StopChildProcess(*s_server_process);
  s_server_process.reset();
  std::cout << "Server stopped\n";
}
