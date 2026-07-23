# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
import sys
import asyncio
import psutil

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

    # And time for register-crash-reporting to complete before inspecting children
    await asyncio.sleep(0.2)

    # Then the Crashpad handler process is running as a child of the repl
    handler_exe_name = 'datadog_crashpad_handler'
    if sys.platform == 'win32':
        handler_exe_name += '.exe'
    proc = psutil.Process(p.pid)
    children = proc.children()
    handler = next((c for c in children if c.name() == handler_exe_name), None)
    assert handler is not None

    p.run("exit")
    await p.join()
    assert p.exitcode == 0
