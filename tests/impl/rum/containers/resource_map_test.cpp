// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/containers/resource_map.hpp"

#include "datadog/rum.hpp"

#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/rum/command.hpp"
#include "datadog/impl/rum/scopes/application.hpp"
#include "datadog/impl/rum/scopes/resource.hpp"
#include "datadog/impl/rum/scopes/session.hpp"
#include "datadog/impl/rum/scopes/view.hpp"

#include "mock/clock.hpp"
#include "mock/system_info.hpp"
#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("RumResourceMap", "[unit][rum]") {
  // Given a DiagnosticLogger that will buffer error messages
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
  DiagnosticLogger diagnostic_logger{
      [&](const DiagnosticMessage& message) {
        if (message.level == DiagnosticLevel::Warning) {
          warnings.push_back(message.text);
        } else if (message.level == DiagnosticLevel::Error) {
          errors.push_back(message.text);
        }
      },
      DiagnosticLevel::Warning
  };

  // And a RumResourceMap configured to use that logger
  RumResourceMap map{diagnostic_logger};
  REQUIRE(warnings.empty());
  REQUIRE(errors.empty());

  // And a set of prerequisite values required to initialize new RumResourceScopes
  RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
  MockClock clock;
  MockSystemInfo system_info;
  CoreContext context(
      CoreConfig{"test-token", "test-service", "test-env"},
      system_info.os_info,
      system_info.device_info
  );
  EventWriter writer = [](Block, Block) { return true; };

  clock.FreezeAtMilliseconds(1700000000000);
  RumScopeDependencies deps(config, clock);
  RumApplicationScope application(deps);
  RumSessionScope session(
      deps,
      application,
      true,
      true,
      *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef"),
      RumSessionPrecondition::UserAppLaunch,
      clock.Now(),
      std::nullopt
  );
  RumViewScope view(
      deps,
      session,
      false,
      *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef"),
      "my-view",
      "My View",
      clock.Now()
  );
  const auto base = [&clock]() {
    return RumCommandParams(clock.Now(), Attribute(), Attribute());
  };

  SECTION("M add new scope w/o error W Add is called") {
    // When we Add a new resource scope into our empty map
    RumResourceScope& resource = map.Add(
        deps,
        view,
        *UUID::Parse("acefe488-129f-430a-b77c-9f93e29fa94a"),
        "foo",
        RumResourceMethod::Post,
        "http://url",
        clock.Now(),
        Attribute()
    );

    // Then we get a valid reference to a newly-created view scope
    REQUIRE(
        resource.GetResourceID() == *UUID::Parse("acefe488-129f-430a-b77c-9f93e29fa94a")
    );
    REQUIRE(resource.GetKey() == "foo");
    REQUIRE(resource.GetMethod() == RumResourceMethod::Post);
    REQUIRE(resource.GetURL() == "http://url");

    // And no warnings or errors are emitted
    REQUIRE(warnings.empty());
    REQUIRE(errors.empty());
  }

  SECTION("{with existing 'foo' and 'bar'}") {
    // Given an existing resource indexed with key 'foo', and another indexed with 'bar'
    map.Add(
        deps,
        view,
        *UUID::Parse("acefe488-129f-430a-b77c-9f93e29fa94a"),
        "foo",
        RumResourceMethod::Post,
        "http://url",
        clock.Now(),
        Attribute()
    );
    map.Add(
        deps,
        view,
        *UUID::Parse("6637dfce-a57b-4141-b3ad-9f5c9c6100d1"),
        "bar",
        RumResourceMethod::Get,
        "http://ok",
        clock.Now(),
        Attribute()
    );
    REQUIRE(map.Size() == 2);

    SECTION("M forward command to target resource W key matches") {
      // When we forward a StopResource command for 'bar' using the key 'bar'
      auto result =
          map.Forward("bar", RumCommand::StopResource(base(), "bar"), context, writer);

      // Then our command is handled and results in a resource event being sent
      REQUIRE(result == RumResourceScope::Result::SentResourceEvent);

      // And we're down to one resource, indicating that 'bar' was stopped
      REQUIRE(map.Size() == 1);

      // And we have no warnings or errors
      REQUIRE(warnings.empty());
      REQUIRE(errors.empty());
    }

    SECTION("M do nothing W command does not match any resource key") {
      // When we forward a StopResource command for 'nobody' using the key 'nobody'
      auto result = map.Forward(
          "nobody", RumCommand::StopResource(base(), "nobody"), context, writer
      );

      // Then our command is ignored, resulting in no event
      REQUIRE(result == RumResourceScope::Result::SentNoEvent);

      // And our map is unmodified
      REQUIRE(map.Size() == 2);

      // And we have no warnings or errors
      REQUIRE(warnings.empty());
      REQUIRE(errors.empty());
    }

    SECTION("M print warning and replace resource W Add is called with duplicate key") {
      // When we attempt to add another resource with key 'foo', which is already in
      // use
      RumResourceScope& resource = map.Add(
          deps,
          view,
          *UUID::Parse("79abc6e8-94cd-47ed-8dd7-11f7a5cc0b09"),
          "foo",
          RumResourceMethod::Delete,
          "http://url/new",
          clock.Now(),
          Attribute()
      );

      // Then we get a warning about the duplicate key
      REQUIRE(warnings.size() == 1);
      REQUIRE(
          warnings.front() ==
          "Replacing existing RUM resource scope with a new one that shares the same "
          "key: application called StartResource twice with same key before "
          "StopResource {\"key\":\"foo\"}"
      );
      REQUIRE(errors.empty());

      // And the resource scope that's now registered with key 'foo' contains our new
      // details, not the state of the old scope
      REQUIRE(
          resource.GetResourceID() ==
          *UUID::Parse("79abc6e8-94cd-47ed-8dd7-11f7a5cc0b09")
      );
      REQUIRE(resource.GetKey() == "foo");
      REQUIRE(resource.GetMethod() == RumResourceMethod::Delete);
      REQUIRE(resource.GetURL() == "http://url/new");
    }
  }
}
