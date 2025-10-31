#!/usr/bin/env bash

# Resolve the path to build/
DD_SDK_ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
CMAKE_BINARY_DIR="$DD_SDK_ROOT_DIR/build"

# Prepare a log file in build/compiler/
mkdir -p "$CMAKE_BINARY_DIR/debug-stats"
LOG_FILE="$CMAKE_BINARY_DIR/debug-stats/compiler-$$"

# Invoke the compiler as configured, writing process status to a file
echo "<debug-compiler> running $1; writing to $LOG_FILE" >&2
if [[ "$OSTYPE" == "darwin"* ]]; then
    /usr/bin/time -l -o "$LOG_FILE" "$@"
else
    /usr/bin/time -v -o "$LOG_FILE" "$@"
fi
COMPILER_EXITCODE=$?
echo "<debug-compiler> finished ($COMPILER_EXITCODE)" >&2

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Command being timed: \"$@\"" >> "$LOG_FILE"
fi
echo "EXITCODE: $COMPILER_EXITCODE" >> "$LOG_FILE"

# Report stats to stderr
if [[ $COMPILER_EXITCODE -eq 137 ]] || grep -q "signal 9" "$LOG_FILE" 2>/dev/null; then
    echo "<debug-debug-compiler> Process killed" >&2
fi
cat "$LOG_FILE" >&2

exit $COMPILER_EXITCODE
