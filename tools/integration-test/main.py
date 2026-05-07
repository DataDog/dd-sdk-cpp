# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
integration-test
Usage: python3 tools/integration-test/main.py

Running this script will:

1. Ensure that the repl binary is present (prerequisite: configure with
   DD_BUILD_EXAMPLES enabled and run a CMake build)
2. Collect the set of test scripts defined in tools/integration-test/tests/
3. Start a proxy that will intercept requests, allowing them to be buffered in-memory as
   they're forwarded to Datadog intake
4. For each test collected in step 2:
    a. Create a temporary storage directory shared by all repl processes in that test
    b. Invoke the test's main function with a TestContext that provides access to the
       storage directory and the ability to spawn repl processes
    c. Validate the test's assertions as the test's async main function drives one or
       more repl processes and inspects their output
5. Print a summary and return 0 if all tests passed; 1 if any test failed
"""
import sys
import asyncio
import argparse
import tempfile
import threading
import traceback
import multiprocessing
from typing import List, Optional

from lib.proxy import ProxyServer
from lib.repl import check_repl_binary
from lib.test import collect_tests, StorageDirectory, TestContext


def _gather_repl_output(ctx: TestContext):
    stdout = '\n'.join(p.stdout for p in ctx._repls if p.stdout)
    stderr = '\n'.join(p.stderr for p in ctx._repls if p.stderr)
    return stdout, stderr


def _print_repl_output(stdout: str, stderr: str = ''):
    if stderr:
        print('--- BEGIN STDERR ---')
        for line in stderr.splitlines():
            if line:
                print(line)
        print('--- END STDERR ---')
    for line in stdout.splitlines():
        if line:
            print(line)


if __name__ == "__main__":
    # Parse command-line args
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', type=str, help='the name of a single test to run; omit to run all tests')
    parser.add_argument('--verbose', '-v', action='store_true', help='If true, print full output for all tests, even if successful')
    parser.add_argument('--jobs', '-j', type=int, default=max(1, multiprocessing.cpu_count() - 2), help='maximum number of tests to run in parallel')
    args = parser.parse_args()

    # repl must already be built (in build/): the CI job should take care of this before
    # running this script
    repl_binary_path = check_repl_binary()

    # Load all integration tests defined in tools/integration-test/tests/
    tests = collect_tests()
    if not tests:
        print('ERROR: No tests found')
        sys.exit(1)

    # If configured to only run a single test, find that test and ignore all the rest
    if args.only:
        matching_test = next((x for x in tests if x.name == args.only), None)
        if not matching_test:
            print('ERROR: No such test: %s' % args.only)
            sys.exit(1)
        tests = [matching_test]

    # Start mitmproxy on a background thread, configured with an addon that will
    # intercept and capture all requests
    proxy_port = 8080
    proxy_url = 'http://127.0.0.1:%d' % proxy_port
    proxy = ProxyServer(proxy_port)
    proxy.start()
    print("Proxy started.")

    # Prepare to run either serially or split across multiple threads, depending on CLI
    # args and number of tests to run
    num_test_threads = 1
    if args.jobs > 1:
        num_test_threads = min(len(tests), args.jobs)

    # Each test result is a tuple of (passed: bool, stdout: str, stderr: str, exc: str|None)
    test_results: List[Optional[tuple]] = [None] * len(tests)

    def run_test(test_index: int):
        test = tests[test_index]
        with tempfile.TemporaryDirectory() as tmpdir:
            storage = StorageDirectory(path=tmpdir)
            ctx = TestContext(storage, repl_binary_path, proxy, proxy_url)
            try:
                asyncio.run(test.func(ctx))
                stdout, stderr = _gather_repl_output(ctx)
                test_results[test_index] = (True, stdout, stderr, None)
            except Exception:
                stdout, stderr = _gather_repl_output(ctx)
                test_results[test_index] = (False, stdout, stderr, traceback.format_exc())

    def run_test_thread(thread_index: int, stride: int):
        test_index = thread_index
        while test_index < len(tests):
            run_test(test_index)
            test_index += stride

    # Run each test's async main function, either serially or across multiple threads
    print('Running %d test(s) on %d thread(s)...' % (len(tests), num_test_threads))
    if num_test_threads == 1:
        for i in range(len(tests)):
            run_test(i)
    else:
        threads: List[threading.Thread] = []
        for i in range(num_test_threads):
            thread = threading.Thread(target=run_test_thread, args=(i, num_test_threads), daemon=True)
            threads.append(thread)
            thread.start()
        for thread in threads:
            thread.join()

    # Stop the proxy
    print("Stopping proxy...")
    proxy.stop()
    print("Proxy stopped.")

    # Print results
    print('')
    num_tests_passed = 0
    for i, test in enumerate(tests):
        result = test_results[i]
        if not result:
            raise RuntimeError(f'No result recorded for test {i}')

        passed, stdout, stderr, exc = result

        if not passed:
            print(f'=== ❌ {test.name} ===')
            _print_repl_output(stdout, stderr)
            if exc:
                print(exc)
            print('')
            continue

        print(f'=== ✅ {test.name} [OK] ===')
        if args.verbose:
            _print_repl_output(stdout, stderr)
            print('')
        num_tests_passed += 1

    # Print a final summary and exit
    print('')
    if num_tests_passed != len(tests):
        print(f'Ran {len(tests)} tests; {num_tests_passed} passed.')
        sys.exit(1)
    print(f'Ran {num_tests_passed} tests; all passed.')
