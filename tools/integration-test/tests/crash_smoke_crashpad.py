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
    """)
    await p.join()
    assert p.exitcode == 0

    # Then the Crashpad database directory was initialized by StartHandler(), confirming
    # that the handler was spawned and the database is ready to receive crash reports.
    # settings.dat is written synchronously by Crashpad during database initialization,
    # before StartHandler() returns, so its presence is a reliable indicator of success.
    crashes_dir = t.storage.get_artifact_dir('.crashes')
    assert (crashes_dir / 'settings.dat').exists(), \
        f'Crashpad database not initialised: {crashes_dir / "settings.dat"} not found'
