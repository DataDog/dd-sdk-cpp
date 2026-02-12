#!/bin/bash

# Run dd_native_repl with a series of commands to trigger a crash
build/examples/dd_native_repl <<EOF
set-config client-token fake-client-token
set-config rum-application-id 1df685d5-43e0-47e5-b6c7-c13aaf2d6fbe
create-core
register-logging
register-rum
register-crash-reporting
start-core
sleep 1
crash $@
sleep 100
EOF
