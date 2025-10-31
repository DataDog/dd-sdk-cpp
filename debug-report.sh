#!/usr/bin/env bash

# Resolve the path to our real clang-tidy binary, stashed in build/ by clang-tidy.cmake
DD_SDK_ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
CMAKE_BINARY_DIR="$DD_SDK_ROOT_DIR/build"

get_exitcode() {
    LOG_FILE="$1"
    grep 'EXITCODE: ' "$LOG_FILE" | awk '{print $NF}'
}

get_command_path() {
    LOG_FILE="$1"
    grep 'Command being timed: ' "$LOG_FILE" | sed 's/^[^"]*"\(.*\)"[^"]*$/\1/' | awk '{print $1}'
}

get_target_file_path() {
    LOG_FILE="$1"
    grep 'Command being timed:' "$LOG_FILE" | sed 's/^[^"]*"\(.*\)"[^"]*$/\1/' | awk '{print $NF}'
}

get_max_rss() {
    LOG_FILE="$1"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        grep 'maximum resident set size' "$LOG_FILE" | awk '{print $1}'
    else
        grep 'Maximum resident set size (kbytes):' "$LOG_FILE" | awk '{print $NF * 1024}'
    fi
}

get_user_time() {
    LOG_FILE="$1"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        grep 'real.*user.*sys' "$LOG_FILE" | awk '{print $3}'
    else
        grep 'User time (seconds):' "$LOG_FILE" | awk '{print $NF}'
    fi
}

get_sys_time() {
    LOG_FILE="$1"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        grep 'real.*user.*sys' "$LOG_FILE" | awk '{print $5}'
    else
        grep 'System time (seconds):' "$LOG_FILE" | awk '{print $NF}'
    fi
}

CSV_FILE="$CMAKE_BINARY_DIR/debug-stats.csv"

echo " === BEGIN $CSV_FILE === "
echo "timestamp,pid,exitcode,command_name,target_file,max_rss,user_time,sys_time" | tee "$CSV_FILE"
SUM_MAX_RSS=0
COUNT=0
while read -r LS_LINE; do
    if [[ "$LS_LINE"* == "total"* ]]; then
        continue
    fi
    ISO_TIMESTAMP=$(echo "$LS_LINE" | awk '{printf "%sT%s%s\n", $6, $7, $8}')
    FILENAME=$(echo "$LS_LINE" | awk '{printf $NF}')
    PID="${FILENAME##*-}"
    LOG_FILE="$CMAKE_BINARY_DIR/debug-stats/$FILENAME"
    EXITCODE=$(get_exitcode "$LOG_FILE")
    COMMAND_PATH=$(get_command_path "$LOG_FILE")
    COMMAND_NAME=$(basename "$COMMAND_PATH")
    TARGET_FILE_PATH=$(get_target_file_path "$LOG_FILE")
    TARGET_FILE_RELPATH=$(realpath -m --relative-to="$DD_SDK_ROOT_DIR" "$TARGET_FILE_PATH")
    MAX_RSS=$(get_max_rss "$LOG_FILE")
    USER_TIME=$(get_user_time "$LOG_FILE")
    SYS_TIME=$(get_sys_time "$LOG_FILE")
    echo "$ISO_TIMESTAMP,$PID,$EXITCODE,$COMMAND_NAME,$TARGET_FILE_RELPATH,$MAX_RSS,$USER_TIME,$SYS_TIME" | tee -a "$CSV_FILE"

    SUM_MAX_RSS=$((SUM_MAX_RSS + MAX_RSS))
    COUNT=$((COUNT + 1))
done < <(ls -ltr --time-style=full-iso "$CMAKE_BINARY_DIR/debug-stats")
echo " === END $CSV_FILE === "

if (( COUNT > 0 )); then
    MEAN_MAX_RSS=$((SUM_MAX_RSS / COUNT))
else
    MEAN_MAX_RSS=0
fi
echo "Sum of max_rss: $SUM_MAX_RSS" >&2
echo "Mean max_rss: $MEAN_MAX_RSS" >&2

NINJA_J_OUTPUT=$(ninja -h 2>&1 | grep '\-j N')
echo "ninja parallel job count (-j): ${NINJA_J_OUTPUT:53}"

echo "Top 20 files with highest max_rss:"
tail -n+2 "$CSV_FILE" | sort -t',' -k6 -n -r | head -n 20

echo "Memory limit: $(cat /sys/fs/cgroup/memory/memory.limit_in_bytes 2>/dev/null || cat /sys/fs/cgroup/memory.max 2>/dev/null || echo 'unknown')" >&2
echo "Memory usage: $(cat /sys/fs/cgroup/memory/memory.usage_in_bytes 2>/dev/null || cat /sys/fs/cgroup/memory.current 2>/dev/null || echo 'unknown')" >&2
free -h >&2 || true
