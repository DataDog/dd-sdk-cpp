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

if [ "$SYMBOL_FORMAT" = "llvm" ]; then
    # llvm-symbolizer: read lines in format "/path/to/binary 0xoffset"
    if [ -n "$CRASH_REPORT" ]; then
        python3 "$SCRIPT_DIR/process_crash_report.py" "$CRASH_REPORT" --format llvm | while IFS= read -r line; do
            if [[ "$line" =~ ^# ]]; then
                printf "%2s  %s\n" "" "$line"
            else
                # llvm-symbolizer reads from stdin, format: /path/to/binary 0xoffset
                result=$(echo "$line" | llvm-symbolizer 2>/dev/null | head -n 1)
                printf "%2d: %s\n" "$frame_num" "$result"
                ((frame_num++))
            fi
        done
    else
        python3 "$SCRIPT_DIR/process_crash_report.py" --format llvm | while IFS= read -r line; do
            if [[ "$line" =~ ^# ]]; then
                printf "%2s  %s\n" "" "$line"
            else
                result=$(echo "$line" | llvm-symbolizer 2>/dev/null | head -n 1)
                printf "%2d: %s\n" "$frame_num" "$result"
                ((frame_num++))
            fi
        done
    fi
else
    # atos (macOS) or addr2line (Linux): execute commands directly
    if [ -n "$CRASH_REPORT" ]; then
        python3 "$SCRIPT_DIR/process_crash_report.py" "$CRASH_REPORT" --format "$SYMBOL_FORMAT" | while IFS= read -r line; do
            if [[ "$line" =~ ^# ]]; then
                printf "%2s  %s\n" "" "$line"
            else
                if [ "$SYMBOL_FORMAT" = "addr2line" ]; then
                    # addr2line -f outputs two lines: function name, then file:line
                    # Capture both and combine them
                    output=$(eval "$line" 2>&1)
                    func_name=$(echo "$output" | head -n 1 | grep -v "^addr2line:" || echo "??")
                    file_line=$(echo "$output" | head -n 2 | tail -n 1 | grep -v "^addr2line:" || echo "??:?")
                    printf "%2d: %s (%s)\n" "$frame_num" "$func_name" "$file_line"
                else
                    # atos outputs a single line
                    result=$(eval "$line" 2>/dev/null || echo "??:?")
                    printf "%2d: %s\n" "$frame_num" "$result"
                fi
                ((frame_num++))
            fi
        done
    else
        python3 "$SCRIPT_DIR/process_crash_report.py" --format "$SYMBOL_FORMAT" | while IFS= read -r line; do
            if [[ "$line" =~ ^# ]]; then
                printf "%2s  %s\n" "" "$line"
            else
                if [ "$SYMBOL_FORMAT" = "addr2line" ]; then
                    # addr2line -f outputs two lines: function name, then file:line
                    # Capture both and combine them
                    output=$(eval "$line" 2>&1)
                    func_name=$(echo "$output" | head -n 1 | grep -v "^addr2line:" || echo "??")
                    file_line=$(echo "$output" | head -n 2 | tail -n 1 | grep -v "^addr2line:" || echo "??:?")
                    printf "%2d: %s (%s)\n" "$frame_num" "$func_name" "$file_line"
                else
                    # atos outputs a single line
                    result=$(eval "$line" 2>/dev/null || echo "??:?")
                    printf "%2d: %s\n" "$frame_num" "$result"
                fi
                ((frame_num++))
            fi
        done
    fi
fi

echo
echo "========================================================================"
