#!/usr/bin/env bash

# Parse options:
# --no-proxy/-P: Refrain from intercepting and dumping requests with mitmproxy
# --no-build/-B: Assume build/dd-sdk-cpp-repl is already build
USE_PROXY="1"
BUILD_SDK="1"
while [[ $# -gt 0 ]]; do
    case $1 in
        -P|--no-proxy)
            USE_PROXY="0"
            shift
            ;;
        -B|--no-build)
            BUILD_SDK="0"
            shift
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--no-proxy|-P] [--no-build|-B]"
            exit 1
            ;;
    esac
done

# Require that tmux is installed
if ! command -v tmux &> /dev/null; then
    echo "ERROR: tmux not found! Please install tmux."
    exit 1
fi

# If using --proxy/-p, require that mitmdump is installed
if [ "$USE_PROXY" == "1" ]; then
    if ! command -v mitmdump &> /dev/null; then
        echo "ERROR: mitmdump not found! Please install mitmproxy or run with --no-proxy."
        exit 1
    fi
fi

# Check for an existing tmux session, and delete it if found
TMUX_SESSION_NAME="dd-sdk-cpp-repl"
if tmux has-session -t "$TMUX_SESSION_NAME" 2>/dev/null; then
    tmux kill-session -t "$TMUX_SESSION_NAME"
fi

# Fail on any command errors henceforth
set -e

# Create a named pipe
PIPE="/tmp/dd-sdk-cpp-repl"
if [ -p "$PIPE" ]; then
    rm "$PIPE"
fi
mkfifo "$PIPE"

# Create a new tmux session
tmux new-session -d -s "$TMUX_SESSION_NAME" -x "$COLUMNS" -y "$LINES"
tmux set-option -t "$TMUX_SESSION_NAME" status off
tmux set-option -t "$TMUX_SESSION_NAME" pane-border-lines heavy
tmux set-option -t "$TMUX_SESSION_NAME" pane-border-style fg=brightblack
tmux set-option -t "$TMUX_SESSION_NAME" pane-active-border-style fg=cyan

# The original (top) pane is the main pane where we'll run the repl
MAIN_PANE=$(tmux display-message -p '#{pane_id}')
tmux send-keys -t "$TMUX_SESSION_NAME:.$MAIN_PANE" "exec 3<> $PIPE" Enter
if [ "$BUILD_SDK" == "1" ]; then
    if [ ! -d "build" ]; then
        tmux send-keys -t "$TMUX_SESSION_NAME:.$MAIN_PANE" 'cmake -GNinja -DCMAKE_BUILD_TYPE=Debug -DDD_DEVELOPMENT=ON -DDD_HTTP_USE_SYSTEM_LIBCURL=ON . -B build && '
    fi
    tmux send-keys -t "$TMUX_SESSION_NAME:.$MAIN_PANE" 'cmake --build build && '
fi
tmux send-keys -t "$TMUX_SESSION_NAME:.$MAIN_PANE" "clear && build/examples/dd_native_repl"
if [ "$USE_PROXY" == "1" ]; then
    tmux send-keys -t "$TMUX_SESSION_NAME:.$MAIN_PANE" " --custom-endpoint-url=http://localhost:8080"
fi
REPL_ARGS=$(printf " %q" "$@")
tmux send-keys -t "$TMUX_SESSION_NAME:.$MAIN_PANE" " $REPL_ARGS < $PIPE" Enter

# Split vertically to create a bottom pane where we'll accept user input
INPUT_PANE=$(tmux split-window -t "$TMUX_SESSION_NAME:.$MAIN_PANE" -v -P -F '#{pane_id}')
tmux resize-pane -t "$TMUX_SESSION_NAME:.$INPUT_PANE" -y 5
if command -v rlwrap &> /dev/null; then
    tmux send-keys -t "$TMUX_SESSION_NAME:.$PROXY_PANE" "rlwrap -a ./repl-input.sh $PIPE" Enter
else
    tmux send-keys -t "$TMUX_SESSION_NAME:.$PROXY_PANE" "./repl-input.sh $PIPE" Enter
fi

# If using --proxy, split the top pane horizontally to create a right pane for mitmdump
if [ "$USE_PROXY" == "1" ]; then
    PROXY_PANE=$(tmux split-window -h -t "$TMUX_SESSION_NAME:.$MAIN_PANE" -P -F '#{pane_id}')
    tmux resize-pane -t "$TMUX_SESSION_NAME:.$PROXY_PANE" -x 57
    tmux send-keys -t "$TMUX_SESSION_NAME:.$PROXY_PANE" 'clear && mitmdump -q -s examples/repl/mitm_dump.py --mode reverse:https://browser-intake-datadoghq.com/ --listen-port 8080' Enter
fi

tmux select-pane -t "$TMUX_SESSION_NAME:.$INPUT_PANE"
tmux attach-session -t "$TMUX_SESSION_NAME"
