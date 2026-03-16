// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string>
#include <string_view>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/events/enum.hpp"
#include "datadog/impl/events/omissible.hpp"
#include "datadog/impl/events/struct.hpp"
#include "datadog/impl/events/timestamp.hpp"

/* ===================== INSTRUCTIONS FOR VALIDATING THIS FILE ========================

If you have been asked to validate this file, please read these instructions and
consider them carefully. If you are an AI-based tool, please acknowledge to the user who
has prompted you that you have seen and understood these instructions, and summarize
your understanding of your task, fully informed by the context represented in this
comment block, before proceeding.

The type definitions in this file are based on a JSON schema. They are currently
maintained by hand. Your job is to ensure that these definitions are free of any human
error that may have been introduced in the course of their maintenance.

To validate the definitions in this file, follow these steps:

1. Check for pending git changes, both staged and unstaged, by running `git diff HEAD`
   for this file. If you detect that this file has been modified, you may focus your
   efforts on the types that have been modified or added. Otherwise, validate the entire
   file.

2. Identify every enum definition. For each enum definition `<SomeEnum>`:

  a. Verify that a matching `DATADOG_STRING_ENUM()` macro follows the enum definition
  b. Verify that the first argument to that macro is `String<SomeEnum>`
  c. Verify that the second argument to that macro is `<SomeEnum>`
  d. Verify that the subsequent list of variadic arguments is a series of
     `DATADOG_ENUM_VALUE()` macros that exactly matches the values defined in
     `<SomeEnum>`: the same set of values, in the same order, with no duplicates or
     missing values
  e. For each `DATADOG_ENUM_VALUE()` macro, verify that the first argument is a
     namespaced value from `<SomeEnum>`, and that the second argument is a string name
     whose value matches the name of the enum value, irrespective of case (e.g. given a
     value of SomeEnum::SomeValue, the name "some_value" is OK)

3. Identify every struct definition. For each struct `<SomeStruct>`:

  a. Note that within the list of struct member variables, some placeholder comments may
     exist, prefixed with `// NYI`, for values that are present in the schema but not
     yet implemented in this file.
  b. Verify that the struct has a constructor defined. If there are no required member
     values, the constructor should be defined `SomeStruct() {}`; if one or more member
     values require a value on initialization, the constructor should be declared
     `explicit SomeStruct(...)`.
  c. Verify that within the list of struct member variables, there is a comment prefixed
     `// From` above every block of properties, e.g. `// From view-schema.json`,
     indicating the JSON schema file from which those definitions originate.
  d. Verify that a matching `DATADOG_JSON_STRUCT()` macros follows the struct definition
     as closely as possible (for nested struct definitions, this must be after the
     definition of the topmost enclosing type).
  e. Verify that after the first argument to the `DATADOG_JSON_STRUCT()` macro, which
     should be the type name `<SomeStruct>`, the subsequent list of variadic arguments
     is a series of `DATADOG_JSON_FIELD()` macros, each naming a member variable in the
     original struct.
  f. Verify that the list of `DATADOG_JSON_FIELD()` macros matches the struct definition
     exactly: the same set of fields, listed in the same order, with the same set of
     header comments, interspersed with the same `// NYI` placeholders.

4. Identify every top-level event type. A top-level event type is a struct type whose
   name takes the form `Rum<Foo>Event`. For every such event:

  a. Read `tests/impl/core/feature_types/rum_test.cpp` and verify that there is a
     top-level `TEST_CASE` declaration matching the name of that type.
  b. Verify that within that `TEST_CASE`, there is a single `SECTION` asserting that a
     `Rum<Foo>Event` serializes to a valid JSON event when all supported values are set.
  c. Examine that `TEST_CASE` and verify that it assigns values to `ev` for every
     leaf-level member variable of `Rum<Foo>Event`, including leaf-level member
     variables of all struct values included as members, recursively. Apply these
     exceptions:
      - Member variables that are declared `const` need not have assignments in the test
      - Member variables with required values that are initialized via a constructor may
        be assigned in the test via a call to `emplace()`, without requiring an explicit
        assignment
      - Properties noted in the struct definition with `// NYI` placeholder comments
        need not be reflected in the test

As soon as you complete Step 1, describe your findings re: pending git changes and
explain the scope with which you will validate the file.

As soon as you complete Step 2, print a succinct list of every enum type analyzed,
prefixed with ✅ or ❌ depending on whether you identified any issues; and list any
issues if found.

As soon as you complete Step 3, print a succinct list of every struct type analyzed,
prefixed with ✅ or ❌ depending on whether you identified any issues; and list any
issues if found.

As soon as you complete Step 4, print a succinct list of every top-level event type you
identified and analyzed, prefixed with ✅ or ❌ depending on whether you identified any
issues; and list any issues if found.

If you entire analysis reveals no issues, conclude by simply printing "✅ OK."

===================================================================================== */

namespace datadog::impl {

/**
 * Additional context for the RUM feature that's made accessible to other features via
 * CoreContext.
 *
 * Other features can read the current RUM feature context in order to access relevant
 * state like the current session ID. This allows those other features to enrich their
 * event payloads with RUM data, which facilitates correlation in the backend.
 */
struct RumFeatureContext {
  UUID application_id;    // UUID::Zero if RUM not initialized
  UUID session_id;        // UUID::Zero if no active session
  UUID view_id;           // UUID::Zero if no active view
  UUID action_id;         // UUID::Zero if no active action
  std::string view_name;  // Empty if no active view

  /**
   * Converts this internal RumFeatureContext to the public RumContext type.
   */
  datadog::RumContextSnapshot ToPublicContext() const {
    return datadog::RumContextSnapshot{
        application_id, session_id, view_id, view_name.c_str(), action_id
    };
  }

  bool operator==(const RumFeatureContext& other) const {
    return application_id == other.application_id && session_id == other.session_id &&
           view_id == other.view_id && action_id == other.action_id &&
           view_name == other.view_name;
  }

  bool operator!=(const RumFeatureContext& other) const { return !(*this == other); }
};

DATADOG_STRING_ENUM(
    StringRumActionType,
    RumActionType,
    DATADOG_ENUM_VALUE(RumActionType::Tap, "tap"),
    DATADOG_ENUM_VALUE(RumActionType::Click, "click"),
    DATADOG_ENUM_VALUE(RumActionType::Scroll, "scroll"),
    DATADOG_ENUM_VALUE(RumActionType::Swipe, "swipe"),
    DATADOG_ENUM_VALUE(RumActionType::Custom, "custom")
)

DATADOG_STRING_ENUM(
    StringRumResourceMethod,
    RumResourceMethod,
    DATADOG_ENUM_VALUE(RumResourceMethod::Get, "GET"),
    DATADOG_ENUM_VALUE(RumResourceMethod::Head, "HEAD"),
    DATADOG_ENUM_VALUE(RumResourceMethod::Post, "POST"),
    DATADOG_ENUM_VALUE(RumResourceMethod::Put, "PUT"),
    DATADOG_ENUM_VALUE(RumResourceMethod::Delete, "DELETE"),
    DATADOG_ENUM_VALUE(RumResourceMethod::Connect, "CONNECT"),
    DATADOG_ENUM_VALUE(RumResourceMethod::Options, "OPTIONS"),
    DATADOG_ENUM_VALUE(RumResourceMethod::Trace, "TRACE"),
    DATADOG_ENUM_VALUE(RumResourceMethod::Patch, "PATCH"),
)

DATADOG_STRING_ENUM(
    StringRumResourceType,
    RumResourceType,
    DATADOG_ENUM_VALUE(RumResourceType::Unknown, "unknown"),
    DATADOG_ENUM_VALUE(RumResourceType::Beacon, "beacon"),
    DATADOG_ENUM_VALUE(RumResourceType::Fetch, "fetch"),
    DATADOG_ENUM_VALUE(RumResourceType::Xhr, "xhr"),
    DATADOG_ENUM_VALUE(RumResourceType::Document, "document"),
    DATADOG_ENUM_VALUE(RumResourceType::Native, "native"),
    DATADOG_ENUM_VALUE(RumResourceType::Image, "image"),
    DATADOG_ENUM_VALUE(RumResourceType::Js, "js"),
    DATADOG_ENUM_VALUE(RumResourceType::Font, "font"),
    DATADOG_ENUM_VALUE(RumResourceType::Css, "css"),
    DATADOG_ENUM_VALUE(RumResourceType::Media, "media"),
    DATADOG_ENUM_VALUE(RumResourceType::Other, "other")
)

/**
 * Indicates why and how a new RUM session was created, tracking the lifecycle
 * transition from the previous session or initial app state. You might also call this
 * the session's "start reason."
 *
 * TODO: This definition is based on the schema for RUM events that's maintained in the
 * rum-events-format repo. Other SDKs have tooling to generate code from these JSON
 * schemas; in this codebase we currently maintain those types by hand.
 */
enum class RumSessionPrecondition : uint8_t {
  /**
   * Session started because the user launched the instrumented application in the
   * foreground (normal interactive launch).
   */
  UserAppLaunch,
  /**
   * Session started automatically after the previous session timed out from lack of
   * user input.
   */
  InactivityTimeout,
  /**
   * Session started automatically after the previous session reached its maximum
   * allowed duration.
   */
  MaxDuration,
  /**
   * Session started because the app was launched in a background state.
   */
  BackgroundLaunch,
  /**
   * Session started because the app was launched by the operating system, without user
   * input, as an optimization.
   */
  Prewarm,
  /**
   * Session started following a previous non-interactive session.
   */
  FromNonInteractiveSession,
  /**
   * Session started after the previous session was explicitly stopped via a
   * StopSession() API call.
   */
  ExplicitStop
};
DATADOG_STRING_ENUM(
    StringRumSessionPrecondition,
    RumSessionPrecondition,
    DATADOG_ENUM_VALUE(RumSessionPrecondition::UserAppLaunch, "user_app_launch"),
    DATADOG_ENUM_VALUE(RumSessionPrecondition::InactivityTimeout, "inactivity_timeout"),
    DATADOG_ENUM_VALUE(RumSessionPrecondition::MaxDuration, "max_duration"),
    DATADOG_ENUM_VALUE(RumSessionPrecondition::BackgroundLaunch, "background_launch"),
    DATADOG_ENUM_VALUE(RumSessionPrecondition::Prewarm, "prewarm"),
    DATADOG_ENUM_VALUE(
        RumSessionPrecondition::FromNonInteractiveSession,
        "from_non_interactive_session"
    ),
    DATADOG_ENUM_VALUE(RumSessionPrecondition::ExplicitStop, "explicit_stop")
)

enum class RumSessionType : uint8_t { User, Synthetics, CITest };
DATADOG_STRING_ENUM(
    StringRumSessionType,
    RumSessionType,
    DATADOG_ENUM_VALUE(RumSessionType::User, "user"),
    DATADOG_ENUM_VALUE(RumSessionType::Synthetics, "synthetics"),
    DATADOG_ENUM_VALUE(RumSessionType::CITest, "ci_test")
)

enum class RumSource : uint8_t {
  Android,
  iOS,
  Browser,
  Flutter,
  ReactNative,
  Roku,
  Unity,
  KotlinMultiplatform,
  RumCpp
};
DATADOG_STRING_ENUM(
    StringRumSource,
    RumSource,
    DATADOG_ENUM_VALUE(RumSource::Android, "android"),
    DATADOG_ENUM_VALUE(RumSource::iOS, "ios"),
    DATADOG_ENUM_VALUE(RumSource::Browser, "browser"),
    DATADOG_ENUM_VALUE(RumSource::Flutter, "flutter"),
    DATADOG_ENUM_VALUE(RumSource::ReactNative, "react-native"),
    DATADOG_ENUM_VALUE(RumSource::Roku, "roku"),
    DATADOG_ENUM_VALUE(RumSource::Unity, "unity"),
    DATADOG_ENUM_VALUE(RumSource::KotlinMultiplatform, "kotlin-multiplatform"),
    DATADOG_ENUM_VALUE(RumSource::RumCpp, "rum-cpp")
)

struct RumUserProperties {
  // From _common-schema.json
  OmitIfEmpty<std::string> id;
  OmitIfEmpty<std::string> name;
  OmitIfEmpty<std::string> email;
  OmitIfEmpty<std::string> anonymous_id;
};
DATADOG_JSON_STRUCT(
    RumUserProperties,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(email),
    DATADOG_JSON_FIELD(anonymous_id)
)

struct RumAccountProperties {
  // From _common-schema.json
  std::string id;
  OmitIfEmpty<std::string> name;

  explicit RumAccountProperties(std::string_view in_id) : id(in_id) {}
};
DATADOG_JSON_STRUCT(
    RumAccountProperties,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(name)
)

enum class RumConnectivityStatus : uint8_t { Connected, NotConnected, Maybe };
DATADOG_STRING_ENUM(
    StringRumConnectivityStatus,
    RumConnectivityStatus,
    DATADOG_ENUM_VALUE(RumConnectivityStatus::Connected, "connected"),
    DATADOG_ENUM_VALUE(RumConnectivityStatus::NotConnected, "not_connected"),
    DATADOG_ENUM_VALUE(RumConnectivityStatus::Maybe, "maybe")
)

struct RumConnectivityProperties {
  // From _common-schema.json
  StringRumConnectivityStatus status;
  // NYI: interfaces
  // NYI: effective_type
  // NYI: cellular

  explicit RumConnectivityProperties(RumConnectivityStatus in_status)
      : status(in_status) {}
};
DATADOG_JSON_STRUCT(
    RumConnectivityProperties,
    // From _common-schema.json
    DATADOG_JSON_FIELD(status)
    // NYI: interfaces
    // NYI: effective_type
    // NYI: cellular
)

struct RumViewportProperties {
  // From _common-schema.json
  int32_t width;
  int32_t height;

  explicit RumViewportProperties(int32_t in_width, int32_t in_height)
      : width(in_width), height(in_height) {}
};
DATADOG_JSON_STRUCT(
    RumViewportProperties,
    // From _common-schema.json
    DATADOG_JSON_FIELD(width),
    DATADOG_JSON_FIELD(height)
)

struct RumSyntheticsProperties {
  // From _common-schema.json
  std::string test_id;
  std::string result_id;
  OmitIfFalse<bool> injected{false};

  explicit RumSyntheticsProperties(
      std::string_view in_test_id, std::string_view in_result_id
  )
      : test_id(in_test_id), result_id(in_result_id) {}
};
DATADOG_JSON_STRUCT(
    RumSyntheticsProperties,
    // From _common-schema.json
    DATADOG_JSON_FIELD(test_id),
    DATADOG_JSON_FIELD(result_id),
    DATADOG_JSON_FIELD(injected)
)

struct RumCITestProperties {
  // From _common-schema.json
  std::string test_execution_id;

  explicit RumCITestProperties(std::string_view in_test_execution_id)
      : test_execution_id(in_test_execution_id) {}
};
DATADOG_JSON_STRUCT(
    RumCITestProperties,
    // From _common-schema.json
    DATADOG_JSON_FIELD(test_execution_id)
)

struct RumOSProperties {
  // From _common-schema.json
  std::string name;
  std::string version;
  OmitIfEmpty<std::string> build;
  std::string version_major;

  explicit RumOSProperties(
      std::string_view in_name,
      std::string_view in_version,
      std::string_view in_version_major
  )
      : name(in_name), version(in_version), version_major(in_version_major) {}
};
DATADOG_JSON_STRUCT(
    RumOSProperties,
    // From _common-schema.json
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(build),
    DATADOG_JSON_FIELD(version_major)
)

enum class RumDeviceType : uint8_t {
  Mobile,
  Desktop,
  Tablet,
  TV,
  GamingConsole,
  Bot,
  Other
};
DATADOG_STRING_ENUM(
    StringRumDeviceType,
    RumDeviceType,
    DATADOG_ENUM_VALUE(RumDeviceType::Mobile, "mobile"),
    DATADOG_ENUM_VALUE(RumDeviceType::Desktop, "desktop"),
    DATADOG_ENUM_VALUE(RumDeviceType::Tablet, "tablet"),
    DATADOG_ENUM_VALUE(RumDeviceType::TV, "tv"),
    DATADOG_ENUM_VALUE(RumDeviceType::GamingConsole, "gaming_console"),
    DATADOG_ENUM_VALUE(RumDeviceType::Bot, "bot"),
    DATADOG_ENUM_VALUE(RumDeviceType::Other, "other")
)

struct RumDeviceProperties {
  // From _common-schema.json
  OmitIfNoValue<StringRumDeviceType> type;
  OmitIfEmpty<std::string> name;
  OmitIfEmpty<std::string> model;
  OmitIfEmpty<std::string> brand;
  OmitIfEmpty<std::string> architecture;
  OmitIfEmpty<std::string> locale;
  // NYI: locales
  OmitIfEmpty<std::string> time_zone;
  OmitIfNoValue<float> battery_level;
  OmitIfNoValue<bool> power_saving_mode;
  OmitIfNoValue<float> brightness_level;

  RumDeviceProperties() {};
};
DATADOG_JSON_STRUCT(
    RumDeviceProperties,
    // From _common-schema.json
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(model),
    DATADOG_JSON_FIELD(brand),
    DATADOG_JSON_FIELD(architecture),
    DATADOG_JSON_FIELD(locale),
    // NYI: locales
    DATADOG_JSON_FIELD(time_zone),
    DATADOG_JSON_FIELD(battery_level),
    DATADOG_JSON_FIELD(power_saving_mode),
    DATADOG_JSON_FIELD(brightness_level)
)

enum class RumViewLoadingType : uint8_t {
  InitialLoad,
  RouteChange,
  ActivityDisplay,
  ActivityRedisplay,
  FragmentDisplay,
  FragmentRedisplay,
  ViewControllerDisplay,
  ViewControllerRedisplay,
};
DATADOG_STRING_ENUM(
    StringRumViewLoadingType,
    RumViewLoadingType,
    DATADOG_ENUM_VALUE(RumViewLoadingType::InitialLoad, "initial_load"),
    DATADOG_ENUM_VALUE(RumViewLoadingType::RouteChange, "route_change"),
    DATADOG_ENUM_VALUE(RumViewLoadingType::ActivityDisplay, "activity_display"),
    DATADOG_ENUM_VALUE(RumViewLoadingType::ActivityRedisplay, "activity_redisplay"),
    DATADOG_ENUM_VALUE(RumViewLoadingType::FragmentDisplay, "fragment_display"),
    DATADOG_ENUM_VALUE(RumViewLoadingType::FragmentRedisplay, "fragment_redisplay"),
    DATADOG_ENUM_VALUE(
        RumViewLoadingType::ViewControllerDisplay, "view_controller_display"
    ),
    DATADOG_ENUM_VALUE(
        RumViewLoadingType::ViewControllerRedisplay, "view_controller_redisplay"
    )
)

struct RumViewEvent {
  struct Application {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> current_locale;

    explicit Application(const UUID& in_id) : id(in_id) {}
  };
  struct Session {
    // From _common-schema.json
    UUID id;
    StringRumSessionType type;
    OmitIfFalse<bool> has_replay{false};
    // From view-schema.json
    OmitIfNoValue<bool> is_active;
    OmitIfFalse<bool> sampled_for_replay{false};

    explicit Session(const UUID& in_id, RumSessionType in_type)
        : id(in_id), type(in_type) {}
  };
  struct View {
    struct Incidence {
      // From view-schema.json
      uint64_t count;

      explicit Incidence(uint64_t in_count) : count(in_count) {}
    };
    struct PerfMetric {
      // From _perf-metric-schema.json
      double min;
      double max;
      double average;
      OmitIfZero<double> metric_max{0.0};

      explicit PerfMetric(double in_min, double in_max, double in_average)
          : min(in_min), max(in_max), average(in_average) {}
    };
    struct Performance {
      struct CLS {
        struct Rect {
          // From _rect-schema.json
          int32_t x;
          int32_t y;
          int32_t width;
          int32_t height;

          explicit Rect(int32_t in_x, int32_t in_y, int32_t in_width, int32_t in_height)
              : x(in_x), y(in_y), width(in_width), height(in_height) {}
        };
        // From _view-performance-schema.json
        double score;
        OmitIfNoValue<uint64_t> timestamp;
        OmitIfEmpty<std::string> target_selector;
        OmitIfNoValue<Rect> previous_rect;
        OmitIfNoValue<Rect> current_rect;

        explicit CLS(double in_score) : score(in_score) {}
      };
      struct FCP {
        // From _view-performance-schema.json
        uint64_t timestamp;

        explicit FCP(uint64_t in_timestamp) : timestamp(in_timestamp) {}
      };
      struct FID {
        // From _view-performance-schema.json
        uint64_t duration;
        uint64_t timestamp;
        OmitIfEmpty<std::string> target_selector;

        explicit FID(uint64_t in_duration, uint64_t in_timestamp)
            : duration(in_duration), timestamp(in_timestamp) {}
      };
      struct INP {
        // From _view-performance-schema.json
        uint64_t duration;
        OmitIfNoValue<uint64_t> timestamp;
        OmitIfEmpty<std::string> target_selector;

        explicit INP(uint64_t in_duration) : duration(in_duration) {}
      };
      struct LCP {
        // From _view-performance-schema.json
        uint64_t timestamp;
        OmitIfEmpty<std::string> target_selector;
        OmitIfEmpty<std::string> resource_url;

        explicit LCP(uint64_t in_timestamp) : timestamp(in_timestamp) {}
      };
      struct FBC {
        // From _view-performance-schema.json
        uint64_t timestamp;

        explicit FBC(uint64_t in_timestamp) : timestamp(in_timestamp) {}
      };
      // From _view-performance-schema.json
      OmitIfNoValue<CLS> cls;
      OmitIfNoValue<FCP> fcp;
      OmitIfNoValue<FID> fid;
      OmitIfNoValue<INP> inp;
      OmitIfNoValue<LCP> lcp;
      OmitIfNoValue<FBC> fbc;

      Performance() {};
    };
    struct Accessibility {
      // From _view-accessibility-schema.json
      OmitIfEmpty<std::string> text_size;
      OmitIfFalse<bool> screen_reader_enabled{false};
      OmitIfFalse<bool> bold_text_enabled{false};
      OmitIfFalse<bool> reduce_transparency_enabled{false};
      OmitIfFalse<bool> reduce_motion_enabled{false};
      OmitIfFalse<bool> button_shapes_enabled{false};
      OmitIfFalse<bool> invert_colors_enabled{false};
      OmitIfFalse<bool> increase_contrast_enabled{false};
      OmitIfFalse<bool> assistive_switch_enabled{false};
      OmitIfFalse<bool> assistive_touch_enabled{false};
      OmitIfFalse<bool> video_autoplay_enabled{false};
      OmitIfFalse<bool> closed_captioning_enabled{false};
      OmitIfFalse<bool> mono_audio_enabled{false};
      OmitIfFalse<bool> shake_to_undo_enabled{false};
      OmitIfFalse<bool> reduced_animations_enabled{false};
      OmitIfFalse<bool> should_differentiate_without_color{false};
      OmitIfFalse<bool> grayscale_enabled{false};
      OmitIfFalse<bool> single_app_mode_enabled{false};
      OmitIfFalse<bool> on_off_switch_labels_enabled{false};
      OmitIfFalse<bool> speak_screen_enabled{false};
      OmitIfFalse<bool> speak_selection_enabled{false};
      OmitIfFalse<bool> rtl_enabled{false};

      Accessibility() {};
    };
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> referrer;
    std::string url;
    OmitIfEmpty<std::string> name;
    // From view-schema.json
    OmitIfNoValue<uint64_t> loading_time;
    OmitIfNoValue<uint64_t> network_settled_time;
    OmitIfNoValue<uint64_t> interaction_to_next_view_time;
    OmitIfNoValue<StringRumViewLoadingType> loading_type;
    uint64_t time_spent;
    // NYI(web): dom_complete
    // NYI(web): dom_content_loaded
    // NYI(web): dom_interactive
    // NYI(web): load_event
    // NYI(web): first_byte
    // NYI: custom_timings
    OmitIfNoValue<bool> is_active;
    OmitIfFalse<bool> is_slow_rendered{false};
    Incidence action;
    Incidence error;
    OmitIfNoValue<Incidence> crash;
    OmitIfNoValue<Incidence> long_task;
    OmitIfNoValue<Incidence> frozen_frame;
    // NYI: slow_frames
    Incidence resource;
    OmitIfNoValue<Incidence> frustration;
    // NYI: in_foreground_periods
    OmitIfZero<uint64_t> memory_average{0ull};
    OmitIfZero<uint64_t> memory_max{0ull};
    OmitIfZero<uint64_t> cpu_ticks_count{0ull};
    OmitIfZero<double> cpu_ticks_per_second{0.0};
    OmitIfZero<double> refresh_rate_average{0.0};
    OmitIfZero<double> refresh_rate_min{0.0};
    OmitIfZero<double> slow_frames_rate{0.0};
    OmitIfZero<double> freeze_rate{0.0};
    OmitIfNoValue<PerfMetric> flutter_build_time;
    OmitIfNoValue<PerfMetric> flutter_raster_time;
    OmitIfNoValue<PerfMetric> js_refresh_rate;
    OmitIfNoValue<Performance> performance;
    OmitIfNoValue<Accessibility> accessibility;

    explicit View(
        UUID in_id,
        std::string_view in_url,
        uint64_t in_time_spent,
        uint64_t in_action_count,
        uint64_t in_error_count,
        uint64_t in_resource_count
    )
        : id(in_id),
          url(in_url),
          time_spent(in_time_spent),
          action(Incidence{in_action_count}),
          error(Incidence{in_error_count}),
          resource(Incidence{in_resource_count}) {}
  };
  struct Display {
    // From _common-schema.json
    OmitIfNoValue<RumViewportProperties> viewport;
    // From view-schema.json
    // NYI(web): scroll

    Display() {};
  };
  struct Internal {
    struct Session {
      // From _common-schema.json
      OmitIfZero<uint8_t> plan{};
      OmitIfNoValue<StringRumSessionPrecondition> session_precondition;

      Session() {};
    };
    struct Configuration {
      // From _common-schema.json
      float session_sample_rate;
      OmitIfNoValue<float> session_replay_sample_rate;
      OmitIfNoValue<float> profiling_sample_rate;
      // From view-schema.json
      OmitIfNoValue<bool> start_session_replay_recording_manually;

      explicit Configuration(float in_session_sample_rate)
          : session_sample_rate(in_session_sample_rate) {}
    };
    // From _common-schema.json
    const uint8_t format_version{2};
    OmitIfNoValue<Session> session;
    OmitIfNoValue<Configuration> configuration;
    OmitIfEmpty<std::string> browser_sdk_version;
    // From view-schema.json
    uint64_t document_version;
    // NYI: page_states
    // NYI: replay_stats
    // NYI: cls
    // NYI: profiling

    explicit Internal(uint64_t in_document_version)
        : document_version(in_document_version) {}
  };
  // From _common-schema.json
  MilliTimestamp date;
  Application application;
  OmitIfEmpty<std::string> service;
  OmitIfEmpty<std::string> version;
  OmitIfEmpty<std::string> build_version;
  OmitIfEmpty<std::string> build_id;
  OmitIfEmpty<std::string> ddtags;
  Session session;
  OmitIfNoValue<StringRumSource> source;
  View view;
  OmitIfNoValue<RumUserProperties> usr;
  OmitIfNoValue<RumAccountProperties> account;
  OmitIfNoValue<RumConnectivityProperties> connectivity;
  OmitIfNoValue<Display> display;
  OmitIfNoValue<RumSyntheticsProperties> synthetics;
  OmitIfNoValue<RumCITestProperties> ci_test;
  OmitIfNoValue<RumOSProperties> os;
  OmitIfNoValue<RumDeviceProperties> device;
  Internal _dd;
  OmitIfZero<Attribute> context;
  // NYI: stream
  // From view-schema.json
  const std::string_view type{"view"};
  // NYI: feature_flags
  // NYI: privacy

  explicit RumViewEvent(
      Timestamp in_date,
      const UUID& in_application_id,
      const UUID& in_session_id,
      RumSessionType in_session_type,
      const UUID& in_view_id,
      std::string_view in_view_url,
      uint64_t in_view_time_spent,
      uint64_t in_view_action_count,
      uint64_t in_view_error_count,
      uint64_t in_view_resource_count,
      uint64_t in_internal_document_version
  )
      : date(in_date),
        application(Application{in_application_id}),
        session(Session{in_session_id, in_session_type}),
        view(
            View{
                in_view_id,
                in_view_url,
                in_view_time_spent,
                in_view_action_count,
                in_view_error_count,
                in_view_resource_count
            }
        ),
        _dd(Internal{in_internal_document_version}) {}
};
DATADOG_JSON_STRUCT(
    RumViewEvent::Application,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(current_locale)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(has_replay),
    // From view-schema.json
    DATADOG_JSON_FIELD(is_active),
    DATADOG_JSON_FIELD(sampled_for_replay)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Incidence,
    // From view-schema.json
    DATADOG_JSON_FIELD(count)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::PerfMetric,
    // From _perf-metric-schema.json
    DATADOG_JSON_FIELD(min),
    DATADOG_JSON_FIELD(max),
    DATADOG_JSON_FIELD(average),
    DATADOG_JSON_FIELD(metric_max)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Performance::CLS::Rect,
    // From _rect-schema.json
    DATADOG_JSON_FIELD(x),
    DATADOG_JSON_FIELD(y),
    DATADOG_JSON_FIELD(width),
    DATADOG_JSON_FIELD(height)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Performance::CLS,
    // From _view-performance-schema.json
    DATADOG_JSON_FIELD(score),
    DATADOG_JSON_FIELD(timestamp),
    DATADOG_JSON_FIELD(target_selector),
    DATADOG_JSON_FIELD(previous_rect),
    DATADOG_JSON_FIELD(current_rect)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Performance::FCP,
    // From _view-performance-schema.json
    DATADOG_JSON_FIELD(timestamp)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Performance::FID,
    // From _view-performance-schema.json
    DATADOG_JSON_FIELD(duration),
    DATADOG_JSON_FIELD(timestamp),
    DATADOG_JSON_FIELD(target_selector)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Performance::INP,
    // From _view-performance-schema.json
    DATADOG_JSON_FIELD(duration),
    DATADOG_JSON_FIELD(timestamp),
    DATADOG_JSON_FIELD(target_selector)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Performance::LCP,
    // From _view-performance-schema.json
    DATADOG_JSON_FIELD(timestamp),
    DATADOG_JSON_FIELD(target_selector),
    DATADOG_JSON_FIELD(resource_url)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Performance::FBC,
    // From _view-performance-schema.json
    DATADOG_JSON_FIELD(timestamp)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Performance,
    // From _view-performance-schema.json
    DATADOG_JSON_FIELD(cls),
    DATADOG_JSON_FIELD(fcp),
    DATADOG_JSON_FIELD(fid),
    DATADOG_JSON_FIELD(inp),
    DATADOG_JSON_FIELD(lcp),
    DATADOG_JSON_FIELD(fbc)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View::Accessibility,
    // From _view-accessibility-schema.json
    DATADOG_JSON_FIELD(text_size),
    DATADOG_JSON_FIELD(screen_reader_enabled),
    DATADOG_JSON_FIELD(bold_text_enabled),
    DATADOG_JSON_FIELD(reduce_transparency_enabled),
    DATADOG_JSON_FIELD(reduce_motion_enabled),
    DATADOG_JSON_FIELD(button_shapes_enabled),
    DATADOG_JSON_FIELD(invert_colors_enabled),
    DATADOG_JSON_FIELD(increase_contrast_enabled),
    DATADOG_JSON_FIELD(assistive_switch_enabled),
    DATADOG_JSON_FIELD(assistive_touch_enabled),
    DATADOG_JSON_FIELD(video_autoplay_enabled),
    DATADOG_JSON_FIELD(closed_captioning_enabled),
    DATADOG_JSON_FIELD(mono_audio_enabled),
    DATADOG_JSON_FIELD(shake_to_undo_enabled),
    DATADOG_JSON_FIELD(reduced_animations_enabled),
    DATADOG_JSON_FIELD(should_differentiate_without_color),
    DATADOG_JSON_FIELD(grayscale_enabled),
    DATADOG_JSON_FIELD(single_app_mode_enabled),
    DATADOG_JSON_FIELD(on_off_switch_labels_enabled),
    DATADOG_JSON_FIELD(speak_screen_enabled),
    DATADOG_JSON_FIELD(speak_selection_enabled),
    DATADOG_JSON_FIELD(rtl_enabled)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::View,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(referrer),
    DATADOG_JSON_FIELD(url),
    DATADOG_JSON_FIELD(name),
    // From view-schema.json
    DATADOG_JSON_FIELD(loading_time),
    DATADOG_JSON_FIELD(network_settled_time),
    DATADOG_JSON_FIELD(interaction_to_next_view_time),
    DATADOG_JSON_FIELD(loading_type),
    DATADOG_JSON_FIELD(time_spent),
    // NYI(web): dom_complete
    // NYI(web): dom_content_loaded
    // NYI(web): dom_interactive
    // NYI(web): load_event
    // NYI(web): first_byte
    // NYI: custom_timings
    DATADOG_JSON_FIELD(is_active),
    DATADOG_JSON_FIELD(is_slow_rendered),
    DATADOG_JSON_FIELD(action),
    DATADOG_JSON_FIELD(error),
    DATADOG_JSON_FIELD(crash),
    DATADOG_JSON_FIELD(long_task),
    DATADOG_JSON_FIELD(frozen_frame),
    // NYI: slow_frames
    DATADOG_JSON_FIELD(resource),
    DATADOG_JSON_FIELD(frustration),
    // NYI: in_foreground_periods
    DATADOG_JSON_FIELD(memory_average),
    DATADOG_JSON_FIELD(memory_max),
    DATADOG_JSON_FIELD(cpu_ticks_count),
    DATADOG_JSON_FIELD(cpu_ticks_per_second),
    DATADOG_JSON_FIELD(refresh_rate_average),
    DATADOG_JSON_FIELD(refresh_rate_min),
    DATADOG_JSON_FIELD(slow_frames_rate),
    DATADOG_JSON_FIELD(freeze_rate),
    DATADOG_JSON_FIELD(flutter_build_time),
    DATADOG_JSON_FIELD(flutter_raster_time),
    DATADOG_JSON_FIELD(js_refresh_rate),
    DATADOG_JSON_FIELD(performance),
    DATADOG_JSON_FIELD(accessibility)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::Display,
    // From _common-schema.json
    DATADOG_JSON_FIELD(viewport)
    // From view-schema.json
    // NYI(web): scroll
)
DATADOG_JSON_STRUCT(
    RumViewEvent::Internal::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(plan),
    DATADOG_JSON_FIELD(session_precondition)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::Internal::Configuration,
    // From _common-schema.json
    DATADOG_JSON_FIELD(session_sample_rate),
    DATADOG_JSON_FIELD(session_replay_sample_rate),
    DATADOG_JSON_FIELD(profiling_sample_rate),
    // From view-schema.json
    DATADOG_JSON_FIELD(start_session_replay_recording_manually)
)
DATADOG_JSON_STRUCT(
    RumViewEvent::Internal,
    // From _common-schema.json
    DATADOG_JSON_FIELD(format_version),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(configuration),
    DATADOG_JSON_FIELD(browser_sdk_version),
    // From view-schema.json
    DATADOG_JSON_FIELD(document_version)
    // NYI: page_states
    // NYI: replay_stats
    // NYI: cls
    // NYI: profiling
)
DATADOG_JSON_STRUCT(
    RumViewEvent,
    // From _common-schema.json
    DATADOG_JSON_FIELD(date),
    DATADOG_JSON_FIELD(application),
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(build_version),
    DATADOG_JSON_FIELD(build_id),
    DATADOG_JSON_FIELD(ddtags),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(source),
    DATADOG_JSON_FIELD(view),
    DATADOG_JSON_FIELD(usr),
    DATADOG_JSON_FIELD(account),
    DATADOG_JSON_FIELD(connectivity),
    DATADOG_JSON_FIELD(display),
    DATADOG_JSON_FIELD(synthetics),
    DATADOG_JSON_FIELD(ci_test),
    DATADOG_JSON_FIELD(os),
    DATADOG_JSON_FIELD(device),
    DATADOG_JSON_FIELD(_dd),
    DATADOG_JSON_FIELD(context),
    // NYI: stream
    // From view-schema.json
    DATADOG_JSON_FIELD(type)
    // NYI: feature_flags
    // NYI: privacy
)

struct RumActionEvent {
  struct Application {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> current_locale;

    explicit Application(const UUID& in_id) : id(in_id) {}
  };
  struct Session {
    // From _common-schema.json
    UUID id;
    StringRumSessionType type;
    OmitIfFalse<bool> has_replay{false};

    explicit Session(const UUID& in_id, RumSessionType in_type)
        : id(in_id), type(in_type) {}
  };
  struct View {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> referrer;
    std::string url;
    OmitIfEmpty<std::string> name;

    // From action-schema.json
    // NYI(browser): in_foreground

    explicit View(UUID in_id, std::string_view in_url) : id(in_id), url(in_url) {}
  };
  struct Display {
    // From _common-schema.json
    OmitIfNoValue<RumViewportProperties> viewport;

    Display() {};
  };
  struct Action {
    struct Target {
      // From action-schema.json
      std::string name;

      explicit Target(std::string_view in_name) : name(in_name) {}
    };
    struct Incidence {
      // From action-schema.json
      uint64_t count;

      explicit Incidence(uint64_t in_count) : count(in_count) {}
    };

    // From action-schema.json
    StringRumActionType type;
    UUID id;
    uint64_t loading_time;
    OmitIfNoValue<Target> target;
    // NYI: frustration
    OmitIfNoValue<Incidence> error;
    OmitIfNoValue<Incidence> crash;
    OmitIfNoValue<Incidence> long_task;
    OmitIfNoValue<Incidence> resource;

    explicit Action(RumActionType in_type, UUID in_id, Duration in_duration)
        : type(in_type), id(in_id), loading_time(in_duration.count()) {}
  };
  struct Internal {
    struct Session {
      // From _common-schema.json
      OmitIfZero<uint8_t> plan{};
      OmitIfNoValue<StringRumSessionPrecondition> session_precondition;

      Session() {};
    };
    struct Configuration {
      // From _common-schema.json
      float session_sample_rate;
      OmitIfNoValue<float> session_replay_sample_rate;
      OmitIfNoValue<float> profiling_sample_rate;

      explicit Configuration(float in_session_sample_rate)
          : session_sample_rate(in_session_sample_rate) {}
    };
    // From _common-schema.json
    const uint8_t format_version{2};
    OmitIfNoValue<Session> session;
    OmitIfNoValue<Configuration> configuration;
    OmitIfEmpty<std::string> browser_sdk_version;

    // From action-schema.json
    // NYI(browser): action
    // NYI(browser): target
    // NYI(browser): name_source

    Internal() {}
  };
  // From _common-schema.json
  MilliTimestamp date;
  Application application;
  OmitIfEmpty<std::string> service;
  OmitIfEmpty<std::string> version;
  OmitIfEmpty<std::string> build_version;
  OmitIfEmpty<std::string> build_id;
  OmitIfEmpty<std::string> ddtags;
  Session session;
  OmitIfNoValue<StringRumSource> source;
  View view;
  OmitIfNoValue<RumUserProperties> usr;
  OmitIfNoValue<RumAccountProperties> account;
  OmitIfNoValue<RumConnectivityProperties> connectivity;
  OmitIfNoValue<Display> display;
  OmitIfNoValue<RumSyntheticsProperties> synthetics;
  OmitIfNoValue<RumCITestProperties> ci_test;
  OmitIfNoValue<RumOSProperties> os;
  OmitIfNoValue<RumDeviceProperties> device;
  Internal _dd;
  OmitIfZero<Attribute> context;
  // NYI: stream

  // From action-schema.json
  const std::string_view type{"action"};
  Action action;

  explicit RumActionEvent(
      Timestamp in_date,
      const UUID& in_application_id,
      const UUID& in_session_id,
      RumSessionType in_session_type,
      const UUID& in_view_id,
      std::string_view in_view_url,
      RumActionType in_action_type,
      const UUID& in_action_id,
      const Duration& in_action_duration
  )
      : date(in_date),
        application(Application{in_application_id}),
        session(Session{in_session_id, in_session_type}),
        view(View{in_view_id, in_view_url}),
        _dd(Internal{}),
        action(Action{in_action_type, in_action_id, in_action_duration}) {}
};
DATADOG_JSON_STRUCT(
    RumActionEvent::Application,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(current_locale)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(has_replay)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::View,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(referrer),
    DATADOG_JSON_FIELD(url),
    DATADOG_JSON_FIELD(name)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::Display,
    // From _common-schema.json
    DATADOG_JSON_FIELD(viewport)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::Internal::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(plan),
    DATADOG_JSON_FIELD(session_precondition)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::Internal::Configuration,
    // From _common-schema.json
    DATADOG_JSON_FIELD(session_sample_rate),
    DATADOG_JSON_FIELD(session_replay_sample_rate),
    DATADOG_JSON_FIELD(profiling_sample_rate)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::Internal,
    // From _common-schema.json
    DATADOG_JSON_FIELD(format_version),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(configuration),
    DATADOG_JSON_FIELD(browser_sdk_version)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::Action::Target,
    // From action-schema.json
    DATADOG_JSON_FIELD(name)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::Action::Incidence,
    // From action-schema.json
    DATADOG_JSON_FIELD(count)
)
DATADOG_JSON_STRUCT(
    RumActionEvent::Action,
    // From action-schema.json
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(loading_time),
    DATADOG_JSON_FIELD(target),
    // NYI: frustration
    DATADOG_JSON_FIELD(error),
    DATADOG_JSON_FIELD(crash),
    DATADOG_JSON_FIELD(long_task),
    DATADOG_JSON_FIELD(resource)
)
DATADOG_JSON_STRUCT(
    RumActionEvent,
    // From _common-schema.json
    DATADOG_JSON_FIELD(date),
    DATADOG_JSON_FIELD(application),
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(build_version),
    DATADOG_JSON_FIELD(build_id),
    DATADOG_JSON_FIELD(ddtags),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(source),
    DATADOG_JSON_FIELD(view),
    DATADOG_JSON_FIELD(usr),
    DATADOG_JSON_FIELD(account),
    DATADOG_JSON_FIELD(connectivity),
    DATADOG_JSON_FIELD(display),
    DATADOG_JSON_FIELD(synthetics),
    DATADOG_JSON_FIELD(ci_test),
    DATADOG_JSON_FIELD(os),
    DATADOG_JSON_FIELD(device),
    DATADOG_JSON_FIELD(_dd),
    DATADOG_JSON_FIELD(context),
    // NYI: stream
    // From action-schema.json
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(action)
)

enum class RumResourceRenderBlockingStatus : uint8_t { Blocking, NonBlocking };
DATADOG_STRING_ENUM(
    StringRumResourceRenderBlockingStatus,
    RumResourceRenderBlockingStatus,
    DATADOG_ENUM_VALUE(RumResourceRenderBlockingStatus::Blocking, "blocking"),
    DATADOG_ENUM_VALUE(RumResourceRenderBlockingStatus::NonBlocking, "non-blocking")
)

enum class RumResourceDeliveryType : uint8_t { Cache, NavigationalPrefetch, Other };
DATADOG_STRING_ENUM(
    StringRumResourceDeliveryType,
    RumResourceDeliveryType,
    DATADOG_ENUM_VALUE(RumResourceDeliveryType::Cache, "cache"),
    DATADOG_ENUM_VALUE(
        RumResourceDeliveryType::NavigationalPrefetch, "navigational-prefetch"
    ),
    DATADOG_ENUM_VALUE(RumResourceDeliveryType::Other, "other")
)

enum class RumResourceProviderType : uint8_t {
  Ad,
  Advertising,
  Analytics,
  Cdn,
  Content,
  CustomerSuccess,
  FirstParty,
  Hosting,
  Marketing,
  Other,
  Social,
  TagManager,
  Utility,
  Video
};
DATADOG_STRING_ENUM(
    StringRumResourceProviderType,
    RumResourceProviderType,
    DATADOG_ENUM_VALUE(RumResourceProviderType::Ad, "ad"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Advertising, "advertising"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Analytics, "analytics"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Cdn, "cdn"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Content, "content"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::CustomerSuccess, "customer-success"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::FirstParty, "first party"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Hosting, "hosting"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Marketing, "marketing"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Other, "other"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Social, "social"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::TagManager, "tag-manager"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Utility, "utility"),
    DATADOG_ENUM_VALUE(RumResourceProviderType::Video, "video")
)

struct RumResourceEvent {
  struct Application {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> current_locale;

    explicit Application(const UUID& in_id) : id(in_id) {}
  };
  struct Session {
    // From _common-schema.json
    UUID id;
    StringRumSessionType type;
    OmitIfFalse<bool> has_replay{false};

    explicit Session(const UUID& in_id, RumSessionType in_type)
        : id(in_id), type(in_type) {}
  };
  struct View {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> referrer;
    std::string url;
    OmitIfEmpty<std::string> name;

    explicit View(UUID in_id, std::string_view in_url) : id(in_id), url(in_url) {}
  };
  struct Display {
    // From _common-schema.json
    OmitIfNoValue<RumViewportProperties> viewport;

    Display() {};
  };
  struct Action {
    // From _action-child-schema.json
    UUID id;  // Schema permits array of UUIDs; we only support a single value

    explicit Action(UUID in_id) : id(in_id) {}
  };
  struct Resource {
    struct Phase {
      // From resource-schema.json
      int64_t duration;
      int64_t start;

      explicit Phase(int64_t in_duration, int64_t in_start)
          : duration(in_duration), start(in_start) {}
    };
    struct Provider {
      // From resource-schema.json
      OmitIfNoValue<std::string> domain;
      OmitIfNoValue<std::string> name;
      OmitIfNoValue<StringRumResourceProviderType> type;

      Provider() {}
    };
    // From resource-schema.json
    UUID id;
    StringRumResourceType type;
    OmitIfNoValue<StringRumResourceMethod> method;
    std::string url;
    OmitIfNoValue<int32_t> status_code;
    OmitIfNoValue<int64_t> duration;
    OmitIfNoValue<int64_t> size;
    OmitIfNoValue<int64_t> encoded_body_size;
    OmitIfNoValue<int64_t> decoded_body_size;
    OmitIfNoValue<int64_t> transfer_size;
    OmitIfNoValue<StringRumResourceRenderBlockingStatus> render_blocking_status;
    OmitIfNoValue<Phase> worker;
    OmitIfNoValue<Phase> redirect;
    OmitIfNoValue<Phase> dns;
    OmitIfNoValue<Phase> connect;
    OmitIfNoValue<Phase> ssl;
    OmitIfNoValue<Phase> first_byte;
    OmitIfNoValue<Phase> download;
    OmitIfEmpty<std::string> protocol;
    OmitIfNoValue<StringRumResourceDeliveryType> delivery_type;
    OmitIfNoValue<Provider> provider;
    // NYI: graphql

    explicit Resource(UUID in_id, RumResourceType in_type, std::string_view in_url)
        : id(in_id), type(in_type), url(in_url) {}
  };
  struct Internal {
    struct Session {
      // From _common-schema.json
      OmitIfZero<uint8_t> plan{};
      OmitIfNoValue<StringRumSessionPrecondition> session_precondition;

      Session() {};
    };
    struct Configuration {
      // From _common-schema.json
      float session_sample_rate;
      OmitIfNoValue<float> session_replay_sample_rate;
      OmitIfNoValue<float> profiling_sample_rate;

      explicit Configuration(float in_session_sample_rate)
          : session_sample_rate(in_session_sample_rate) {}
    };
    // From _common-schema.json
    const uint8_t format_version{2};
    OmitIfNoValue<Session> session;
    OmitIfNoValue<Configuration> configuration;
    OmitIfEmpty<std::string> browser_sdk_version;

    // From resource-schema.json
    OmitIfEmpty<std::string> span_id;
    OmitIfEmpty<std::string> parent_span_id;
    OmitIfEmpty<std::string> trace_id;
    OmitIfNoValue<float> rule_psr;
    OmitIfNoValue<bool> discarded;

    Internal() {}
  };
  // From _common-schema.json
  MilliTimestamp date;
  Application application;
  OmitIfEmpty<std::string> service;
  OmitIfEmpty<std::string> version;
  OmitIfEmpty<std::string> build_version;
  OmitIfEmpty<std::string> build_id;
  OmitIfEmpty<std::string> ddtags;
  Session session;
  OmitIfNoValue<StringRumSource> source;
  View view;
  OmitIfNoValue<RumUserProperties> usr;
  OmitIfNoValue<RumAccountProperties> account;
  OmitIfNoValue<RumConnectivityProperties> connectivity;
  OmitIfNoValue<Display> display;
  OmitIfNoValue<RumSyntheticsProperties> synthetics;
  OmitIfNoValue<RumCITestProperties> ci_test;
  OmitIfNoValue<RumOSProperties> os;
  OmitIfNoValue<RumDeviceProperties> device;
  Internal _dd;
  OmitIfZero<Attribute> context;
  // NYI: stream

  // From _action-child-schema.json
  OmitIfNoValue<Action> action;

  // From resource-schema.json
  const std::string_view type{"resource"};
  Resource resource;

  explicit RumResourceEvent(
      Timestamp in_date,
      const UUID& in_application_id,
      const UUID& in_session_id,
      RumSessionType in_session_type,
      const UUID& in_view_id,
      std::string_view in_view_url,
      const UUID& in_resource_id,
      RumResourceType in_resource_type,
      std::string_view in_resource_url
  )
      : date(in_date),
        application(Application{in_application_id}),
        session(Session{in_session_id, in_session_type}),
        view(View{in_view_id, in_view_url}),
        _dd(Internal{}),
        resource(in_resource_id, in_resource_type, in_resource_url) {}
};
DATADOG_JSON_STRUCT(
    RumResourceEvent::Application,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(current_locale)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(has_replay)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::View,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(referrer),
    DATADOG_JSON_FIELD(url),
    DATADOG_JSON_FIELD(name)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Display,
    // From _common-schema.json
    DATADOG_JSON_FIELD(viewport)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Internal::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(plan),
    DATADOG_JSON_FIELD(session_precondition)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Internal::Configuration,
    // From _common-schema.json
    DATADOG_JSON_FIELD(session_sample_rate),
    DATADOG_JSON_FIELD(session_replay_sample_rate),
    DATADOG_JSON_FIELD(profiling_sample_rate)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Internal,
    // From _common-schema.json
    DATADOG_JSON_FIELD(format_version),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(configuration),
    DATADOG_JSON_FIELD(browser_sdk_version),
    // From resource-schema.json
    DATADOG_JSON_FIELD(span_id),
    DATADOG_JSON_FIELD(parent_span_id),
    DATADOG_JSON_FIELD(trace_id),
    DATADOG_JSON_FIELD(rule_psr),
    DATADOG_JSON_FIELD(discarded)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Action,
    // From _action-child-schema.json
    DATADOG_JSON_FIELD(id)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Resource::Phase,
    // From resource-schema.json
    DATADOG_JSON_FIELD(duration),
    DATADOG_JSON_FIELD(start)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Resource::Provider,
    // From resource-schema.json
    DATADOG_JSON_FIELD(domain),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(type)
)
DATADOG_JSON_STRUCT(
    RumResourceEvent::Resource,
    // From resource-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(method),
    DATADOG_JSON_FIELD(url),
    DATADOG_JSON_FIELD(status_code),
    DATADOG_JSON_FIELD(duration),
    DATADOG_JSON_FIELD(size),
    DATADOG_JSON_FIELD(encoded_body_size),
    DATADOG_JSON_FIELD(decoded_body_size),
    DATADOG_JSON_FIELD(transfer_size),
    DATADOG_JSON_FIELD(render_blocking_status),
    DATADOG_JSON_FIELD(worker),
    DATADOG_JSON_FIELD(redirect),
    DATADOG_JSON_FIELD(dns),
    DATADOG_JSON_FIELD(connect),
    DATADOG_JSON_FIELD(ssl),
    DATADOG_JSON_FIELD(first_byte),
    DATADOG_JSON_FIELD(download),
    DATADOG_JSON_FIELD(protocol),
    DATADOG_JSON_FIELD(delivery_type),
    DATADOG_JSON_FIELD(provider)
    // NYI: graphql
)
DATADOG_JSON_STRUCT(
    RumResourceEvent,
    // From _common-schema.json
    DATADOG_JSON_FIELD(date),
    DATADOG_JSON_FIELD(application),
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(build_version),
    DATADOG_JSON_FIELD(build_id),
    DATADOG_JSON_FIELD(ddtags),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(source),
    DATADOG_JSON_FIELD(view),
    DATADOG_JSON_FIELD(usr),
    DATADOG_JSON_FIELD(account),
    DATADOG_JSON_FIELD(connectivity),
    DATADOG_JSON_FIELD(display),
    DATADOG_JSON_FIELD(synthetics),
    DATADOG_JSON_FIELD(ci_test),
    DATADOG_JSON_FIELD(os),
    DATADOG_JSON_FIELD(device),
    DATADOG_JSON_FIELD(_dd),
    DATADOG_JSON_FIELD(context),
    // NYI: stream
    // From _action-child-schema.json
    DATADOG_JSON_FIELD(action),
    // From resource-schema.json
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(resource)
)

DATADOG_STRING_ENUM(
    StringRumErrorSource,
    RumErrorSource,
    DATADOG_ENUM_VALUE(RumErrorSource::Network, "network"),
    DATADOG_ENUM_VALUE(RumErrorSource::Source, "source"),
    DATADOG_ENUM_VALUE(RumErrorSource::Console, "console"),
    DATADOG_ENUM_VALUE(RumErrorSource::Logger, "logger"),
    DATADOG_ENUM_VALUE(RumErrorSource::Agent, "agent"),
    DATADOG_ENUM_VALUE(RumErrorSource::Webview, "webview"),
    DATADOG_ENUM_VALUE(RumErrorSource::Custom, "custom"),
    DATADOG_ENUM_VALUE(RumErrorSource::Report, "report")
)

enum class RumErrorCategory : uint8_t {
  Anr,
  AppHang,
  Exception,
  WatchdogTermination,
  MemoryWarning,
  Network
};
DATADOG_STRING_ENUM(
    StringRumErrorCategory,
    RumErrorCategory,
    DATADOG_ENUM_VALUE(RumErrorCategory::Anr, "ANR"),
    DATADOG_ENUM_VALUE(RumErrorCategory::AppHang, "App Hang"),
    DATADOG_ENUM_VALUE(RumErrorCategory::Exception, "Exception"),
    DATADOG_ENUM_VALUE(RumErrorCategory::WatchdogTermination, "Watchdog Termination"),
    DATADOG_ENUM_VALUE(RumErrorCategory::MemoryWarning, "Memory Warning"),
    DATADOG_ENUM_VALUE(RumErrorCategory::Network, "Network")
)

enum class RumErrorHandling : uint8_t { Handled, Unhandled };
DATADOG_STRING_ENUM(
    StringRumErrorHandling,
    RumErrorHandling,
    DATADOG_ENUM_VALUE(RumErrorHandling::Handled, "handled"),
    DATADOG_ENUM_VALUE(RumErrorHandling::Unhandled, "unhandled")
)

enum class RumCspDisposition : uint8_t { Enforce, Report };
DATADOG_STRING_ENUM(
    StringRumCspDisposition,
    RumCspDisposition,
    DATADOG_ENUM_VALUE(RumCspDisposition::Enforce, "enforce"),
    DATADOG_ENUM_VALUE(RumCspDisposition::Report, "report")
)

enum class RumErrorSourceType : uint8_t {
  Android,
  Browser,
  iOS,
  ReactNative,
  Flutter,
  Roku,
  NDK,
  iOS_IL2CPP,
  NDK_IL2CPP
};
DATADOG_STRING_ENUM(
    StringRumErrorSourceType,
    RumErrorSourceType,
    DATADOG_ENUM_VALUE(RumErrorSourceType::Android, "android"),
    DATADOG_ENUM_VALUE(RumErrorSourceType::Browser, "browser"),
    DATADOG_ENUM_VALUE(RumErrorSourceType::iOS, "ios"),
    DATADOG_ENUM_VALUE(RumErrorSourceType::ReactNative, "react-native"),
    DATADOG_ENUM_VALUE(RumErrorSourceType::Flutter, "flutter"),
    DATADOG_ENUM_VALUE(RumErrorSourceType::Roku, "roku"),
    DATADOG_ENUM_VALUE(RumErrorSourceType::NDK, "ndk"),
    DATADOG_ENUM_VALUE(RumErrorSourceType::iOS_IL2CPP, "ios+il2cpp"),
    DATADOG_ENUM_VALUE(RumErrorSourceType::NDK_IL2CPP, "ndk+il2cpp")
)

struct RumErrorEvent {
  struct Application {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> current_locale;

    explicit Application(const UUID& in_id) : id(in_id) {}
  };
  struct Session {
    // From _common-schema.json
    UUID id;
    StringRumSessionType type;
    OmitIfFalse<bool> has_replay{false};

    explicit Session(const UUID& in_id, RumSessionType in_type)
        : id(in_id), type(in_type) {}
  };
  struct View {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> referrer;
    std::string url;
    OmitIfEmpty<std::string> name;
    // From error-schema.json
    OmitIfNoValue<bool> in_foreground;

    explicit View(UUID in_id, std::string_view in_url) : id(in_id), url(in_url) {}
  };
  struct Display {
    // From _common-schema.json
    OmitIfNoValue<RumViewportProperties> viewport;

    Display() {};
  };
  struct Action {
    // From _action-child-schema.json
    UUID id;  // Schema permits array of UUIDs; we only support a single value

    explicit Action(UUID in_id) : id(in_id) {}
  };
  struct Error {
    struct Resource {
      // From error-schema.json
      StringRumResourceMethod method;
      int32_t status_code;
      std::string url;
      OmitIfNoValue<RumResourceEvent::Resource::Provider> provider;

      explicit Resource(
          RumResourceMethod in_method, int32_t in_status_code, std::string_view in_url
      )
          : method(in_method), status_code(in_status_code), url(in_url) {}
    };
    struct Meta {
      // From error-schema.json
      OmitIfEmpty<std::string> code_type;
      OmitIfEmpty<std::string> parent_process;
      OmitIfEmpty<std::string> incident_identifier;
      OmitIfEmpty<std::string> process;
      OmitIfEmpty<std::string> exception_type;
      OmitIfEmpty<std::string> exception_codes;
      OmitIfEmpty<std::string> path;

      Meta() {};
    };
    struct Csp {
      // From error-schema.json
      OmitIfNoValue<StringRumCspDisposition> disposition;

      Csp() {};
    };
    // From error-schema.json
    OmitIfZero<UUID> id;
    std::string message;
    StringRumErrorSource source;
    OmitIfEmpty<std::string> stack;
    // NYI(array): causes
    OmitIfNoValue<bool> is_crash;
    OmitIfEmpty<std::string> fingerprint;
    OmitIfEmpty<std::string> type;
    OmitIfNoValue<StringRumErrorCategory> category;
    OmitIfNoValue<StringRumErrorHandling> handling;
    OmitIfEmpty<std::string> handling_stack;
    OmitIfNoValue<StringRumErrorSourceType> source_type;
    OmitIfNoValue<Resource> resource;
    // NYI(array): threads
    // NYI(array): binary_images
    OmitIfNoValue<bool> was_truncated;
    OmitIfNoValue<Meta> meta;
    OmitIfNoValue<Csp> csp;
    OmitIfZero<int64_t> time_since_app_start{0};

    explicit Error(std::string_view in_message, RumErrorSource in_source)
        : message(in_message), source(in_source) {}
  };
  struct Freeze {
    // From error-schema.json
    int64_t duration;
  };
  struct Internal {
    struct Session {
      // From _common-schema.json
      OmitIfZero<uint8_t> plan{};
      OmitIfNoValue<StringRumSessionPrecondition> session_precondition;

      Session() {};
    };
    struct Configuration {
      // From _common-schema.json
      float session_sample_rate;
      OmitIfNoValue<float> session_replay_sample_rate;
      OmitIfNoValue<float> profiling_sample_rate;

      explicit Configuration(float in_session_sample_rate)
          : session_sample_rate(in_session_sample_rate) {}
    };
    // From _common-schema.json
    const uint8_t format_version{2};
    OmitIfNoValue<Session> session;
    OmitIfNoValue<Configuration> configuration;
    OmitIfEmpty<std::string> browser_sdk_version;

    Internal() {}
  };
  // From _common-schema.json
  MilliTimestamp date;
  Application application;
  OmitIfEmpty<std::string> service;
  OmitIfEmpty<std::string> version;
  OmitIfEmpty<std::string> build_version;
  OmitIfEmpty<std::string> build_id;
  OmitIfEmpty<std::string> ddtags;
  Session session;
  OmitIfNoValue<StringRumSource> source;
  View view;
  OmitIfNoValue<RumUserProperties> usr;
  OmitIfNoValue<RumAccountProperties> account;
  OmitIfNoValue<RumConnectivityProperties> connectivity;
  OmitIfNoValue<Display> display;
  OmitIfNoValue<RumSyntheticsProperties> synthetics;
  OmitIfNoValue<RumCITestProperties> ci_test;
  OmitIfNoValue<RumOSProperties> os;
  OmitIfNoValue<RumDeviceProperties> device;
  Internal _dd;
  OmitIfZero<Attribute> context;
  // NYI: stream

  // From _action-child-schema.json
  OmitIfNoValue<Action> action;

  // From error-schema.json
  const std::string_view type{"error"};
  Error error;
  OmitIfNoValue<Freeze> freeze;
  // NYI: feature_flags

  explicit RumErrorEvent(
      Timestamp in_date,
      const UUID& in_application_id,
      const UUID& in_session_id,
      RumSessionType in_session_type,
      const UUID& in_view_id,
      std::string_view in_view_url,
      std::string_view in_error_message,
      RumErrorSource in_error_source
  )
      : date(in_date),
        application(Application{in_application_id}),
        session(Session{in_session_id, in_session_type}),
        view(View{in_view_id, in_view_url}),
        _dd(Internal{}),
        error(in_error_message, in_error_source) {}
};
DATADOG_JSON_STRUCT(
    RumErrorEvent::Application,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(current_locale)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(has_replay)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::View,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(referrer),
    DATADOG_JSON_FIELD(url),
    DATADOG_JSON_FIELD(name),
    // From error-schema.json
    DATADOG_JSON_FIELD(in_foreground)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Display,
    // From _common-schema.json
    DATADOG_JSON_FIELD(viewport)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Internal::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(plan),
    DATADOG_JSON_FIELD(session_precondition)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Internal::Configuration,
    // From _common-schema.json
    DATADOG_JSON_FIELD(session_sample_rate),
    DATADOG_JSON_FIELD(session_replay_sample_rate),
    DATADOG_JSON_FIELD(profiling_sample_rate)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Internal,
    // From _common-schema.json
    DATADOG_JSON_FIELD(format_version),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(configuration),
    DATADOG_JSON_FIELD(browser_sdk_version)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Action,
    // From _action-child-schema.json
    DATADOG_JSON_FIELD(id)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Error::Resource,
    // From error-schema.json
    DATADOG_JSON_FIELD(method),
    DATADOG_JSON_FIELD(status_code),
    DATADOG_JSON_FIELD(url),
    DATADOG_JSON_FIELD(provider)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Error::Meta,
    // From error-schema.json
    DATADOG_JSON_FIELD(code_type),
    DATADOG_JSON_FIELD(parent_process),
    DATADOG_JSON_FIELD(incident_identifier),
    DATADOG_JSON_FIELD(process),
    DATADOG_JSON_FIELD(exception_type),
    DATADOG_JSON_FIELD(exception_codes),
    DATADOG_JSON_FIELD(path)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Error::Csp,
    // From error-schema.json
    DATADOG_JSON_FIELD(disposition)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Error,
    // From error-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(message),
    DATADOG_JSON_FIELD(source),
    DATADOG_JSON_FIELD(stack),
    // NYI: causes
    DATADOG_JSON_FIELD(is_crash),
    DATADOG_JSON_FIELD(fingerprint),
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(category),
    DATADOG_JSON_FIELD(handling),
    DATADOG_JSON_FIELD(handling_stack),
    DATADOG_JSON_FIELD(source_type),
    DATADOG_JSON_FIELD(resource),
    // NYI: threads
    // NYI: binary_images
    DATADOG_JSON_FIELD(was_truncated),
    DATADOG_JSON_FIELD(meta),
    DATADOG_JSON_FIELD(csp),
    DATADOG_JSON_FIELD(time_since_app_start)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent::Freeze,
    // From error-schema.json
    DATADOG_JSON_FIELD(duration)
)
DATADOG_JSON_STRUCT(
    RumErrorEvent,
    // From _common-schema.json
    DATADOG_JSON_FIELD(date),
    DATADOG_JSON_FIELD(application),
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(build_version),
    DATADOG_JSON_FIELD(build_id),
    DATADOG_JSON_FIELD(ddtags),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(source),
    DATADOG_JSON_FIELD(view),
    DATADOG_JSON_FIELD(usr),
    DATADOG_JSON_FIELD(account),
    DATADOG_JSON_FIELD(connectivity),
    DATADOG_JSON_FIELD(display),
    DATADOG_JSON_FIELD(synthetics),
    DATADOG_JSON_FIELD(ci_test),
    DATADOG_JSON_FIELD(os),
    DATADOG_JSON_FIELD(device),
    DATADOG_JSON_FIELD(_dd),
    DATADOG_JSON_FIELD(context),
    // NYI: stream
    // From _action-child-schema.json
    DATADOG_JSON_FIELD(action),
    // From error-schema.json
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(error),
    DATADOG_JSON_FIELD(freeze)
    // NYI: feature_flags
)

// === Vital Operation Step Event ===

enum class RumVitalType : uint8_t { OperationStep };
DATADOG_STRING_ENUM(
    StringRumVitalType,
    RumVitalType,
    DATADOG_ENUM_VALUE(RumVitalType::OperationStep, "operation_step")
)

enum class RumVitalStepType : uint8_t { Start, Update, Retry, End };
DATADOG_STRING_ENUM(
    StringRumVitalStepType,
    RumVitalStepType,
    DATADOG_ENUM_VALUE(RumVitalStepType::Start, "start"),
    DATADOG_ENUM_VALUE(RumVitalStepType::Update, "update"),
    DATADOG_ENUM_VALUE(RumVitalStepType::Retry, "retry"),
    DATADOG_ENUM_VALUE(RumVitalStepType::End, "end")
)

enum class RumVitalFailureReason : uint8_t { Error, Abandoned, Other };
DATADOG_STRING_ENUM(
    StringRumVitalFailureReason,
    RumVitalFailureReason,
    DATADOG_ENUM_VALUE(RumVitalFailureReason::Error, "error"),
    DATADOG_ENUM_VALUE(RumVitalFailureReason::Abandoned, "abandoned"),
    DATADOG_ENUM_VALUE(RumVitalFailureReason::Other, "other")
)

struct RumVitalEvent {
  struct Application {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> current_locale;

    explicit Application(const UUID& in_id) : id(in_id) {}
  };
  struct Session {
    // From _common-schema.json
    UUID id;
    StringRumSessionType type;
    OmitIfFalse<bool> has_replay{false};

    explicit Session(const UUID& in_id, RumSessionType in_type)
        : id(in_id), type(in_type) {}
  };
  struct View {
    // From _common-schema.json
    UUID id;
    OmitIfEmpty<std::string> referrer;
    std::string url;
    OmitIfEmpty<std::string> name;

    explicit View(UUID in_id, std::string_view in_url) : id(in_id), url(in_url) {}
  };
  struct Display {
    // From _common-schema.json
    OmitIfNoValue<RumViewportProperties> viewport;

    Display() {};
  };
  struct Vital {
    // From _vital-common-schema.json
    UUID id;
    std::string name;
    // From vital-operation-step-schema.json
    StringRumVitalType type;
    StringRumVitalStepType step_type;
    OmitIfEmpty<std::string> operation_key;
    OmitIfNoValue<StringRumVitalFailureReason> failure_reason;

    explicit Vital(
        const UUID& in_id,
        std::string_view in_name,
        RumVitalType in_type,
        RumVitalStepType in_step_type
    )
        : id(in_id), name(in_name), type(in_type), step_type(in_step_type) {}
  };
  struct Internal {
    struct Session {
      // From _common-schema.json
      OmitIfZero<uint8_t> plan{};
      OmitIfNoValue<StringRumSessionPrecondition> session_precondition;

      Session() {};
    };
    struct Configuration {
      // From _common-schema.json
      float session_sample_rate;
      OmitIfNoValue<float> session_replay_sample_rate;
      OmitIfNoValue<float> profiling_sample_rate;

      explicit Configuration(float in_session_sample_rate)
          : session_sample_rate(in_session_sample_rate) {}
    };
    // From _common-schema.json
    const uint8_t format_version{2};
    OmitIfNoValue<Session> session;
    OmitIfNoValue<Configuration> configuration;
    OmitIfEmpty<std::string> browser_sdk_version;

    Internal() {}
  };
  // From _common-schema.json
  MilliTimestamp date;
  Application application;
  OmitIfEmpty<std::string> service;
  OmitIfEmpty<std::string> version;
  OmitIfEmpty<std::string> build_version;
  OmitIfEmpty<std::string> build_id;
  OmitIfEmpty<std::string> ddtags;
  Session session;
  OmitIfNoValue<StringRumSource> source;
  View view;
  OmitIfNoValue<RumUserProperties> usr;
  OmitIfNoValue<RumAccountProperties> account;
  OmitIfNoValue<RumConnectivityProperties> connectivity;
  OmitIfNoValue<Display> display;
  OmitIfNoValue<RumSyntheticsProperties> synthetics;
  OmitIfNoValue<RumCITestProperties> ci_test;
  OmitIfNoValue<RumOSProperties> os;
  OmitIfNoValue<RumDeviceProperties> device;
  Internal _dd;
  OmitIfZero<Attribute> context;
  // NYI: stream

  // From _vital-common-schema.json
  const std::string_view type{"vital"};
  Vital vital;

  explicit RumVitalEvent(
      Timestamp in_date,
      const UUID& in_application_id,
      const UUID& in_session_id,
      RumSessionType in_session_type,
      const UUID& in_view_id,
      std::string_view in_view_url,
      const UUID& in_vital_id,
      RumVitalType in_vital_type,
      std::string_view in_vital_name,
      RumVitalStepType in_step_type
  )
      : date(in_date),
        application(Application{in_application_id}),
        session(Session{in_session_id, in_session_type}),
        view(View{in_view_id, in_view_url}),
        _dd(Internal{}),
        vital(Vital{in_vital_id, in_vital_name, in_vital_type, in_step_type}) {}
};
DATADOG_JSON_STRUCT(
    RumVitalEvent::Application,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(current_locale)
)
DATADOG_JSON_STRUCT(
    RumVitalEvent::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(has_replay)
)
DATADOG_JSON_STRUCT(
    RumVitalEvent::View,
    // From _common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(referrer),
    DATADOG_JSON_FIELD(url),
    DATADOG_JSON_FIELD(name)
)
DATADOG_JSON_STRUCT(
    RumVitalEvent::Display,
    // From _common-schema.json
    DATADOG_JSON_FIELD(viewport)
)
DATADOG_JSON_STRUCT(
    RumVitalEvent::Vital,
    // From _vital-common-schema.json
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(name),
    // From vital-operation-step-schema.json
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(step_type),
    DATADOG_JSON_FIELD(operation_key),
    DATADOG_JSON_FIELD(failure_reason)
)
DATADOG_JSON_STRUCT(
    RumVitalEvent::Internal::Session,
    // From _common-schema.json
    DATADOG_JSON_FIELD(plan),
    DATADOG_JSON_FIELD(session_precondition)
)
DATADOG_JSON_STRUCT(
    RumVitalEvent::Internal::Configuration,
    // From _common-schema.json
    DATADOG_JSON_FIELD(session_sample_rate),
    DATADOG_JSON_FIELD(session_replay_sample_rate),
    DATADOG_JSON_FIELD(profiling_sample_rate)
)
DATADOG_JSON_STRUCT(
    RumVitalEvent::Internal,
    // From _common-schema.json
    DATADOG_JSON_FIELD(format_version),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(configuration),
    DATADOG_JSON_FIELD(browser_sdk_version)
)
DATADOG_JSON_STRUCT(
    RumVitalEvent,
    // From _common-schema.json
    DATADOG_JSON_FIELD(date),
    DATADOG_JSON_FIELD(application),
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(build_version),
    DATADOG_JSON_FIELD(build_id),
    DATADOG_JSON_FIELD(ddtags),
    DATADOG_JSON_FIELD(session),
    DATADOG_JSON_FIELD(source),
    DATADOG_JSON_FIELD(view),
    DATADOG_JSON_FIELD(usr),
    DATADOG_JSON_FIELD(account),
    DATADOG_JSON_FIELD(connectivity),
    DATADOG_JSON_FIELD(display),
    DATADOG_JSON_FIELD(synthetics),
    DATADOG_JSON_FIELD(ci_test),
    DATADOG_JSON_FIELD(os),
    DATADOG_JSON_FIELD(device),
    DATADOG_JSON_FIELD(_dd),
    DATADOG_JSON_FIELD(context),
    // NYI: stream
    // From _vital-common-schema.json
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(vital)
)

}  // namespace datadog::impl
