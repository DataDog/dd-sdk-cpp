#pragma once

// This file defines WITH_DATADOG_STRICT_THREADING_CHECKS, which controls whether we
// assert on specific observed behavior in thread safety tests.

// These tests, tagged with [thread-safety], are mainly intended to exercise SDK
// functionality in a multi-threaded environment so that we can validate (via TSan and
// basic smoke testing) that our thread-safety guarantees hold.

// Some of these tests also assert on expected behavior: e.g. 'thread A wrote some of
// these events, and thread B wrote the remainder'. This can be useful for
// sanity-checking behavior during development, and these asserts can be designed so
// that they rarely yield false positives, but they're still timing-dependent and so are
// inherently flaky. Therefore, we disable these more pedantic checks by default.

// Build with -DDD_ENABLE_STRICT_THREADING_CHECKS=ON to override.

#ifndef WITH_DATADOG_STRICT_THREADING_CHECKS
#define WITH_DATADOG_STRICT_THREADING_CHECKS 0
#endif
