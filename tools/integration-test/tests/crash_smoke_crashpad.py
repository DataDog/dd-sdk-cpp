# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
from lib.test import TestContext

# This test only runs when the SDK was compiled with DD_CRASH_MODE=crashpad
CRASH_MODE = 'crashpad'


async def main(t: TestContext):
    """
    CrashReporting (Crashpad): smoke test
    """
    # Given a repl process with crash reporting registered and the core started
    p = t.spawn_repl()
    p.run("""
    set-config client-token fake-client-token
    create-core tracking-consent:granted
    register-crash-reporting
    start-core
    crash raise
    """)

    # When the repl crashes
    await p.join()
    assert p.exitcode != 0

    # Then the Crashpad database directory was initialized by StartHandler()
    crashes_dir = t.storage.get_artifact_dir('.crashes')
    assert (crashes_dir / 'settings.dat').exists(), \
        f'Crashpad database not initialized: {crashes_dir / "settings.dat"} not found'

    # And the Crashpad handler POSTed the minidump to the intake endpoint.
    # The handler uploads out-of-process, but join() drains the proxy after the process
    # exits, by which point the upload has already completed.
    assert len(p.requests) == 1, \
        f'Expected 1 request from Crashpad handler, got {len(p.requests)}'
    upload_request = p.requests[0]
    assert upload_request.method == 'POST'
    assert upload_request.url.path == '/crashpad-ingest-placeholder-path'
    # Header name lookup is case-insensitive: Crashpad sends 'Content-Type' (title case)
    content_type = next(
        (v for k, v in upload_request.headers.items() if k.lower() == 'content-type'), ''
    )
    assert content_type.startswith('multipart/form-data'), \
        f'Expected multipart/form-data, got: {content_type!r}'
