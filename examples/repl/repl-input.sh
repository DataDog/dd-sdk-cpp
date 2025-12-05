#!/usr/bin/env bash
PIPE="$1"
clear
echo '> '
echo '> '
echo '> '
echo '> '
while true; do
    read -p '> ' INPUT
    echo "$INPUT" > "$PIPE"
    if [ "$INPUT" == "exit" ]; then
        tmux kill-session
    fi
done
