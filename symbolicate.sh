#!/bin/bash
#
# Symbolicate crash reports (cross-platform)
#
# Usage:
#   ./symbolicate.sh [crash_report_path]
#
# If no crash report path is provided, the script will use the most recent
# crash report from the .crashes/ directory.
#
# Platform support:
#   - macOS: uses atos
#   - Linux: uses addr2line (or llvm-symbolizer if available)

set -euo pipefail

# Determine the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Detect platform and determine symbolication tool
case "$(uname -s)" in
    Darwin*)
        PLATFORM="macos"
        SYMBOL_FORMAT="atos"
        ;;
    Linux*)
        PLATFORM="linux"
        # Prefer llvm-symbolizer if available, otherwise use addr2line
        if command -v llvm-symbolizer >/dev/null 2>&1; then
            SYMBOL_FORMAT="llvm"
            SYMBOLIZER_CMD="llvm-symbolizer"
        else
            SYMBOL_FORMAT="addr2line"
            SYMBOLIZER_CMD="addr2line"
        fi
        ;;
    *)
        echo "Error: Unsupported platform: $(uname -s)" >&2
        exit 1
        ;;
esac

# Optional crash report path
CRASH_REPORT="${1:-}"

# Build the base command
if [ -n "$CRASH_REPORT" ]; then
    PROCESS_CMD="python3 \"$SCRIPT_DIR/process_crash_report.py\" \"$CRASH_REPORT\""
else
    PROCESS_CMD="python3 \"$SCRIPT_DIR/process_crash_report.py\""
fi

echo "========================================================================"
echo "Crash Report Analysis"
echo "========================================================================"
echo

# Show the human-readable crash report details
eval "$PROCESS_CMD"

echo
echo "========================================================================"
echo "Symbolicated Stack Trace"
echo "========================================================================"
echo

# Generate and execute symbolication commands
frame_num=0

# Determine crash report argument
if [ -n "$CRASH_REPORT" ]; then
    REPORT_ARG="$CRASH_REPORT"
else
    REPORT_ARG=""
fi

# Process each line from the crash report
python3 "$SCRIPT_DIR/process_crash_report.py" $REPORT_ARG --format "$SYMBOL_FORMAT" | while IFS= read -r line; do
    # Skip comment lines
    if [[ "$line" =~ ^# ]]; then
        printf "%2s  %s\n" "" "$line"
        continue
    fi

    # Execute symbolication command (disable exit-on-error temporarily)
    set +e
    if [ "$SYMBOL_FORMAT" = "atos" ]; then
        # atos outputs a single line
        result=$(eval "$line" 2>/dev/null || echo "??:?")
        printf "%2d: %s\n" "$frame_num" "$result"
    else
        # addr2line and llvm-symbolizer both output two lines: function name, then file:line
        # Combine them into a single line
        output=$(eval "$line" 2>&1 || true)
        func_name=$(echo "$output" | head -n 1 | { grep -v "^addr2line:" || true; } | head -n 1)
        file_line=$(echo "$output" | head -n 2 | tail -n 1 | { grep -v "^addr2line:" || true; })

        # Use defaults if empty
        [ -z "$func_name" ] && func_name="??"
        [ -z "$file_line" ] && file_line="??:?"

        printf "%2d: %s (%s)\n" "$frame_num" "$func_name" "$file_line"
    fi

    ((frame_num++))
    set -e
done

echo
echo "========================================================================"
