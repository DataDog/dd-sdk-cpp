#!/bin/bash
#
# Symbolicate crash reports on macOS using atos
#
# Usage:
#   ./symbolicate-macos.sh [crash_report_path]
#
# If no crash report path is provided, the script will use the most recent
# crash report from the .crashes/ directory.

set -euo pipefail

# Determine the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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

# Generate and execute atos commands for symbolication
frame_num=0
if [ -n "$CRASH_REPORT" ]; then
    python3 "$SCRIPT_DIR/process_crash_report.py" "$CRASH_REPORT" --format atos | while IFS= read -r line; do
        # Skip comment lines
        if [[ "$line" =~ ^# ]]; then
            printf "%2s  %s\n" "" "$line"
        else
            # Execute the atos command and prefix with frame number
            result=$(eval "$line")
            printf "%2d: %s\n" "$frame_num" "$result"
            ((frame_num++))
        fi
    done
else
    python3 "$SCRIPT_DIR/process_crash_report.py" --format atos | while IFS= read -r line; do
        # Skip comment lines
        if [[ "$line" =~ ^# ]]; then
            printf "%2s  %s\n" "" "$line"
        else
            # Execute the atos command and prefix with frame number
            result=$(eval "$line")
            printf "%2d: %s\n" "$frame_num" "$result"
            ((frame_num++))
        fi
    done
fi

echo
echo "========================================================================"
