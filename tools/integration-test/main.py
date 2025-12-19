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
4. For each test script collected in step 2:
    a. Invoke the repl binary, configured to with a custom endpoint URL that will send
       all SDK uploads to our proxy, with a 'sdk-%d' path prefix identifying the client
    b. Feed the test's commands into that repl process
    c. Pause as needed to validate the test's assertions, effectively retrieving the set
       of requests that passed through the proxy and validating that they contain the
       expected event data
5. Print a summary and return 0 if all tests passed; 1 if any test failed
"""
import sys
import argparse
import threading
import traceback
import multiprocessing
from typing import List, Optional

from lib.proxy import ProxyServer
from lib.test import collect_tests, TestInput
from lib.repl import check_repl_binary, check_repl_env, run_repl, ReplResult


def _print_repl_output(stdout: str, stderr: str):
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

    # repl must already be built (in build/) and configured with a .repl-env file (in
    # the root of the repo): the CI job should take care of this before running this
    # script
    check_repl_binary()
    check_repl_env()

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

    # Construct a pre-sized list to contain the results of the repl invocation for each
    # of the tests that we'll run, and prepare functions to run each test's repl
    # commands and store the repl output and exitcode
    repl_results: List[Optional[ReplResult]] = [None] * len(tests)

    def run_test(test_index: int):
        test = tests[test_index]
        sdk_id = test_index
        repl_results[test_index] = run_repl(proxy_url, sdk_id, test.script)

    def run_test_thread(thread_index: int, stride: int):
        test_index = thread_index
        while test_index < len(tests):
            run_test(test_index)
            test_index += stride

    # Run a repl process for each configured test
    print('Running %d test(s) on %d thread(s)...' % (len(tests), num_test_threads))
    if num_test_threads == 1:
        for i, test in enumerate(tests):
            run_test(i)
    else:
        threads: List[threading.Thread] = []
        for i in range(num_test_threads):
            thread = threading.Thread(target=run_test_thread, args=(i, num_test_threads), daemon=True)
            threads.append(thread)
            thread.start()
        for thread in threads:
            thread.join()

    # Stop the proxy and retrieve our buffered list of all requests that were
    # intercepted by the proxy while our tests were running
    print("Stopping proxy...")
    all_requests = proxy.stop()
    print("Proxy stopped.")

    # Iterate through all the tests we ran, printing relevant output and outright
    # failing any tests that did not successfully complete their repl commands
    print('')
    num_tests_passed = 0
    for i, test in enumerate(tests):
        # Check the results of this test's repl invocation to see if it completed
        # without errors or warnings
        res = repl_results[i]
        if not res:
            raise RuntimeError(f'No repl results recorded for test {i}')

        # Filter our captured requests to get only the requests sent from this test
        requests = [x for x in all_requests if x.sdk_id == i]

        # If repl printed anything to stderr or returned with a nonzero exit code,
        # consider the test failed
        if not res.ok:
            print(f'=== ❌ {test.name} [repl exitcode: {res.exitcode}] ===')
            _print_repl_output(res.stdout, res.stderr)
            print('')
            continue

        # If repl invocation succeeded, allow the test's assertion function to inspect
        # the requests and validate that the SDK produced the expected events: if the
        # function raises any exception, fail the test
        try:
            test.assert_func(TestInput(requests))
        except Exception:
            print(f'=== ❌ {test.name} [test assertions failed] ===')
            _print_repl_output(res.stdout, res.stderr)
            print(traceback.format_exc())
            continue

        # repl exited OK and assertions passed; this test is OK
        print(f'=== ✅ {test.name} [OK] ===')
        if args.verbose:
            _print_repl_output(res.stdout, res.stderr)
            print('')
        num_tests_passed += 1

    # Print a final summary and exit
    print('')
    if num_tests_passed != len(tests):
        print(f'Ran {len(tests)} tests; {num_tests_passed} passed.')
        sys.exit(1)
    print(f'Ran {num_tests_passed} tests; all passed.')
