// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/containers/view_array.hpp"

#include "datadog/rum.hpp"
#include "diagnostics.hpp"
#include "features/rum/command.hpp"
#include "features/rum/scopes/application.hpp"
#include "features/rum/scopes/session.hpp"
#include "features/rum/scopes/view.hpp"
#include "mock/clock.hpp"
#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

static UUID uuid_from_index(uint8_t i) {
  UUID value;
  value.bytes[15] = i;
  return value;
}

static std::string_view key_from_index(uint8_t i) {
  static char buf[64];
  snprintf(buf, sizeof(buf), "view-%" PRIu8, i);
  return std::string_view{static_cast<const char*>(buf)};
}

TEST_CASE("RumViewArray", "[unit][rum]") {
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

  // And a RumViewArray configured to use that logger
  RumViewArray array{diagnostic_logger};
  REQUIRE(warnings.empty());
  REQUIRE(errors.empty());
  auto count_items = [&array]() {
    return std::count_if(array.items.begin(), array.items.end(), [](const auto& item) {
      return item.has_value();
    });
  };

  // And a set of prerequisite values required to initialize new RumViewScopes
  RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
  MockClock clock;
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
  const auto base = [&clock]() {
    return RumCommandParams(clock.Now(), Attribute(), Attribute());
  };

  SECTION("{size=0}") {
    // Given an empty array
    REQUIRE(count_items() == 0);

    SECTION("M add new scope w/o error W Push is called") {
      // When we Push a new view scope onto our empty array
      RumViewScope& view = array.Push(
          deps, session, false, uuid_from_index(65), key_from_index(65), "", clock.Now()
      );

      // Then we get a valid reference to a newly-created view scope
      REQUIRE(view.GetViewID() == *UUID::Parse("00000000-0000-0000-0000-000000000041"));
      REQUIRE(view.GetKey() == "view-65");

      // And no warnings or errors are emitted
      REQUIRE(warnings.empty());
      REQUIRE(errors.empty());

      // And (implementation detail) our scope is stored at index 0
      REQUIRE(array.items[0].has_value());
      REQUIRE(array.items[0]->GetKey() == "view-65");
      REQUIRE(count_items() == 1);
    }

    SECTION("M do nothing W Propagate is called") {
      // When we propagate a command to our set of zero child scopes
      array.Propagate(RumCommand::StartAction(base(), RumActionType::Custom, "foo"));

      // Then nothing happens
      REQUIRE(count_items() == 0);
    }
  }

  SECTION("{size=2}") {
    // Given an array with two views:
    // - At T+10ms: 00000000-0000-0000-0000-000000000000 (view-0)
    // - At T+20ms: 00000000-0000-0000-0000-000000000001 (view-1)
    clock.TickMilliseconds(10);
    array.Push(
        deps, session, false, uuid_from_index(0), key_from_index(0), "", clock.Now()
    );
    clock.TickMilliseconds(10);
    array.Push(
        deps, session, false, uuid_from_index(1), key_from_index(1), "", clock.Now()
    );
    REQUIRE(count_items() == 2);

    SECTION("M add new scope w/o error W Push is called") {
      // When we Push a new view scope onto our 2-element array
      RumViewScope& view = array.Push(
          deps, session, false, uuid_from_index(65), key_from_index(65), "", clock.Now()
      );

      // Then we get a valid reference to a newly-created view scope
      REQUIRE(view.GetViewID() == *UUID::Parse("00000000-0000-0000-0000-000000000041"));
      REQUIRE(view.GetKey() == "view-65");

      // And no warnings or errors are emitted
      REQUIRE(warnings.empty());
      REQUIRE(errors.empty());

      // And (implementation detail) our scope is stored at index 2
      REQUIRE(array.items[2].has_value());
      REQUIRE(array.items[2]->GetKey() == "view-65");
      REQUIRE(count_items() == 3);
    }

    SECTION("M pass command to all scopes W Propagate is called {both RemainOpen}") {
      // When we propagate a command that will update both our scopes and keep them open
      REQUIRE(array.items[0].has_value());
      REQUIRE(array.items[0]->GetActiveAction() == std::nullopt);
      REQUIRE(array.items[1].has_value());
      REQUIRE(array.items[1]->GetActiveAction() == std::nullopt);
      array.Propagate(RumCommand::StartAction(base(), RumActionType::Custom, "foo"));

      // Then our scopes remain alive, and their state is updated in response to the
      // command
      REQUIRE(count_items() == 2);
      REQUIRE(array.items[0]->GetActiveAction() != std::nullopt);
      REQUIRE(array.items[1]->GetActiveAction() != std::nullopt);
    }

    SECTION("M pass command to all scopes W Propagate is called {both Close}") {
      // When we propagate a command that will cause both our scopes to close
      array.Propagate(RumCommand::StopSession(base()));

      // Then both scopes are removed from the array
      REQUIRE(count_items() == 0);
    }

    SECTION(
        "M pass command to all scopes W Propagate is called {0:Close, 1:RemainOpen}"
    ) {
      // When we propagate a command that will cause view-0 to be closed but keep view-1
      // open
      array.Propagate(RumCommand::StartView(base(), "view-1", "view-1"));

      // Then one scope is removed from the array
      REQUIRE(count_items() == 1);

      // And the slot previously occupied by view-0 is now open once again
      REQUIRE(!array.items[0].has_value());

      // And view-1 remains at its original location in the array
      REQUIRE(array.items[1].has_value());
      REQUIRE(array.items[1]->GetKey() == "view-1");
    }
  }

  SECTION("{size=MAX}") {
    // Given an array at max capacity, populated with views spaced 10ms apart and:
    // - UUID: ...0000, ...0001, ...0002, ..., ...000{n-1}
    // - Key: view-0, view-1, view-2, ..., view-{n-1}
    static_assert(
        array.items.max_size() < std::numeric_limits<uint8_t>::max(),
        "RumViewArray capacity exceeds 0xff"
    );
    for (size_t i = 0; i < array.items.max_size(); i++) {
      clock.TickMilliseconds(10);
      array.Push(
          deps,
          session,
          false,
          uuid_from_index(static_cast<uint8_t>(i)),
          key_from_index(static_cast<uint8_t>(i)),
          "",
          clock.Now()
      );
    }
    REQUIRE(count_items() == array.items.max_size());

    SECTION("M add new scope, replacing oldest w/ warning, W Push is called") {
      // When we Push a new view scope onto our full array
      RumViewScope& view = array.Push(
          deps, session, false, uuid_from_index(65), key_from_index(65), "", clock.Now()
      );

      // Then we get a valid reference to a newly-created view scope
      REQUIRE(view.GetViewID() == *UUID::Parse("00000000-0000-0000-0000-000000000041"));
      REQUIRE(view.GetKey() == "view-65");

      // And we get a warning indicating that the oldest view, view-0, was ejected
      REQUIRE(warnings.size() == 1);
      REQUIRE(
          warnings.front() ==
          "Prematurely purging oldest RUM view scope: too many inactive views are "
          "still awaiting resource completion while new views are being started "
          "{\"backpressure_on\":\"rum-view-scope-array\",\"array_index\":0,\"array_"
          "capacity\":8,\"view_id\":\"00000000-0000-0000-0000-000000000000\",\"view_"
          "key\":\"view-0\"}"
      );
      REQUIRE(errors.empty());

      // And (implementation detail) our scope is stored at index 0
      REQUIRE(array.items[0].has_value());
      REQUIRE(array.items[0]->GetKey() == "view-65");
      REQUIRE(count_items() == array.items.max_size());
    }

    SECTION("M pass command to all scopes W Propagate is called {all RemainOpen}") {
      // When we propagate a command that will update all our scopes and keep them open
      array.Propagate(RumCommand::StartAction(base(), RumActionType::Custom, "foo"));

      // Then all our scopes remain alive
      REQUIRE(count_items() == array.items.max_size());
    }

    SECTION("M pass command to all scopes W Propagate is called {all Close}") {
      // When we propagate a command that will cause all our scopes to close
      array.Propagate(RumCommand::StopSession(base()));

      // Then all scopes are removed from the array
      REQUIRE(count_items() == 0);
    }
  }
}
