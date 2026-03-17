// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/context_thread.hpp"

#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

// This file tests the basic operation of the context thread. Feature implementations
// use FeatureScope to offload work to the context thread: for more extensive test
// coverage of that behavior, including queue processing that actually happens on a
// background thread, with related thread-safety tests, see feature_scope_test.cpp.

TEST_CASE("ContextThreadMain", "[unit]") {
  // Given a CoreContextProvider and a DiagnosticLogger
  CoreContextProvider context_provider(MOCK_CONTEXT);
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger diagnostic_logger = diagnostics.CreateTestLogger();

  // And a queue containing functions to be executed by the context thread
  Queue<std::function<void()>> context_queue;

  SECTION("M log debug messages W started and stopped cleanly") {
    // When we run ContextThreadMain synchronously on an empty, stopped queue
    context_queue.Stop();
    ContextThreadMain(diagnostic_logger, context_queue);

    // Then we should have exactly two debug messages that signal thread start and stop
    REQUIRE(diagnostics.debug.size() == 2);
    REQUIRE(diagnostics.debug[0] == "Context thread starting");
    REQUIRE(diagnostics.debug[1] == "Context thread finished");

    // And there should be no other diagnostic messages besides those two
    REQUIRE(diagnostics.TotalSize() == 2);
  }

  SECTION("M call enqueued functions") {
    // When we enqueue a couple of functions that increment x, then run
    // ContextThreadMain synchronously
    int x = 0;
    context_queue.Push([&x]() { x++; });
    context_queue.Push([&x]() { x++; });
    context_queue.Stop();
    ContextThreadMain(diagnostic_logger, context_queue);

    // Then we should have no unexpected diagnostic output
    REQUIRE(diagnostics.debug.size() == 2);
    REQUIRE(diagnostics.debug.size() == diagnostics.TotalSize());

    // And our increment function should have run twice
    REQUIRE(x == 2);
  }
}
