# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
from lib.test import TestInput


def main(t: TestInput):
    """
    logging: smoke test

    Performs a basic smoke test to verify that we can register the logging feature,
    create a logger, and generate log events.
    ---
    # Configure the repl for integration tests
    source .repl-env
    set-config tracking-consent granted
    set-config service dd-sdk-cpp-repl
    set-config flush-on-stop

    # Start an SDK instance configured with logging as its only feature, with a single
    # logger created before core start
    create-core
    register-logging
    create-logger
    start-core

    # Emit a single log message
    log "Hello from the logging smoke test"

    # Shut down the core and exit, flushing requests on SDK stop
    stop-core
    """
    # We should have received a single HTTP request for our log event
    assert len(t.requests) == 1
    assert t.requests[0].method == 'POST'
    assert t.requests[0].url.path == '/api/v2/logs'

    # And the SDK should have sent a single log event that conveys our message
    events = t.events('/api/v2/logs')
    assert len(events) == 1
    assert events[0]['message'] == 'Hello from the logging smoke test'
