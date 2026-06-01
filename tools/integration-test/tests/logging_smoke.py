# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
import os

from lib.test import TestContext


async def main(t: TestContext):
    """
    Logging: smoke test

    Performs a basic smoke test to verify that we can register the logging feature,
    create a logger, and generate log events.
    """
    # Given a process with an SDK instance configured to send events on shutdown
    p = t.spawn_repl()
    p.run("""
        set-config client-token fake-client-token
        set-config flush-on-stop  
        create-core tracking-consent:granted
    """)

    # And a started Core with the logging feature registered
    p.run("""
        register-logging
        create-logger
        start-core
    """)

    # And user and account details configured
    p.run("""
        set-user-info user-123 name:"Jane Doe" email:jane@example.com
        set-account-info acct-456 name:"Bits"
    """)

    # When we emit a log message, stop the core, and exit (triggering flush-on-stop)
    p.run("""
        log "Hello from the logging smoke test"
        stop-core
        exit
    """)
    await p.join()

    # The the process exits successfully
    assert p.exitcode == 0, f'repl exited with code {p.exitcode}\n{p.stderr}'

    # And no batches of log events remain on disk
    assert len(os.listdir(t.storage.get_pending_events_dir(p.pid, 'logs'))) == 0
    assert len(os.listdir(t.storage.get_granted_events_dir(p.pid, 'logs'))) == 0

    # And we've received a single HTTP request for our log event
    assert len(p.requests) == 1
    assert p.requests[0].method == 'POST'
    assert p.requests[0].url.path == '/api/v2/logs'

    # And that request contains a single log event that conveys our message
    events = [e for req in p.requests if req.url.path == '/api/v2/logs' for e in req.json]
    assert len(events) == 1
    assert events[0]['message'] == 'Hello from the logging smoke test'

    # And the log event includes the user info we set
    assert events[0]['usr']['id'] == 'user-123'
    assert events[0]['usr']['name'] == 'Jane Doe'
    assert events[0]['usr']['email'] == 'jane@example.com'

    # And the log event includes the account info we set
    assert events[0]['account']['id'] == 'acct-456'
    assert events[0]['account']['name'] == 'Bits'
