# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
import os
import sys
import platform
import re
import uuid

from lib.test import TestContext


async def main(t: TestContext):
    """
    TrackingConsent: process migration
    """
    # Given an initial process that had pending consent, and that emits a log event
    # before shutting down normally
    p1 = t.spawn_repl()
    p1.run("""
    set-config client-token fake-client-token
    set-config flush-on-stop
    create-core tracking-consent:pending
    register-logging
    create-logger
    start-core
    log "Hello from the original process"
    sleep 10
    stop-core
    exit
    """)
    await p1.join()
    assert p1.exitcode == 0

    # When we start another process, which should take ownership of the previous
    # process's data; and we create an SDK instance with granted consent, which should
    # make that prior event data eligible for upload; and then we shut down normally
    # (while flushing uploads) without generating any events ourselves
    p2 = t.spawn_repl()
    p2.run("""
    set-config client-token fake-client-token
    set-config flush-on-stop
    create-core tracking-consent:granted
    register-logging
    start-core
    sleep 10
    stop-core
    exit
    """)
    await p2.join()
    assert p2.exitcode == 0

    # Then the original process made no HTTP requests
    assert len(p1.requests) == 0

    # And the second process uploaded the log event from the first process
    assert len(p2.requests) == 1
    assert p2.requests[0].method == 'POST'
    assert p2.requests[0].url.path == '/api/v2/logs'
    events = p2.requests[0].json
    assert len(events) == 1
    assert events[0]['message'] == 'Hello from the original process'
