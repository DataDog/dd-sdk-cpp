#!/usr/bin/env bash

# Resolve the path to our real clang-tidy binary, stashed in build/ by clang-tidy.cmake
DD_SDK_ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
CMAKE_BINARY_DIR="$DD_SDK_ROOT_DIR/build"
CLANG_TIDY_PATH=$(cat "$CMAKE_BINARY_DIR/clang-tidy-path")

# Prepare a build/clang-tidy/ directory to contain stats from individual invocations
mkdir -p "$CMAKE_BINARY_DIR/debug-stats"
LOG_FILE="$CMAKE_BINARY_DIR/debug-stats/clang-tidy-$$"

# Attempt to figure out K8s-imposed memory limits
echo "<debug-clang-tidy> System resource limits:" >&2
echo "Memory limit: $(cat /sys/fs/cgroup/memory/memory.limit_in_bytes 2>/dev/null || cat /sys/fs/cgroup/memory.max 2>/dev/null || echo 'unknown')" >&2
echo "Memory usage: $(cat /sys/fs/cgroup/memory/memory.usage_in_bytes 2>/dev/null || cat /sys/fs/cgroup/memory.current 2>/dev/null || echo 'unknown')" >&2
free -h >&2 || true

# Forward all args to the actual clang-tidy, using time to instrument the process and
# write stats to a log file. Use -l on macOS, -v on other platforms (Linux).
echo "<debug-clang-tidy> running clang-tidy; writing to $LOG_FILE" >&2
if [[ "$OSTYPE" == "darwin"* ]]; then
    /usr/bin/time -l -o "$LOG_FILE" "$CLANG_TIDY_PATH" "$@"
else
    /usr/bin/time -v -o "$LOG_FILE" "$CLANG_TIDY_PATH" "$@"
fi
CLANG_TIDY_EXITCODE=$?
echo "<debug-clang-tidy> clang-tidy finished ($CLANG_TIDY_EXITCODE)" >&2

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Command being timed: \"clang-tidy $@\"" >> "$LOG_FILE"
fi
echo "EXITCODE: $CLANG_TIDY_EXITCODE" >> "$LOG_FILE"

# Echo the contents of the log file for this process
if [[ $CLANG_TIDY_EXITCODE -eq 137 ]] || grep -q "signal 9" "$LOG_FILE" 2>/dev/null; then
    echo "<debug-clang-tidy> Process killed" >&2
fi
cat "$LOG_FILE" >&2

# Propagate the clang-tidy exit code
exit $CLANG_TIDY_EXITCODE
