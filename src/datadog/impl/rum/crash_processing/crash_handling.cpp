// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/crash_handling.hpp"

#include <charconv>
#include <chrono>

#include "datadog/impl/core/feature_types/crash_reporting.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/json.hpp"
#include "datadog/impl/core/version.hpp"
#include "datadog/impl/rum/crash_processing/crash_formatting.hpp"
#include "datadog/impl/rum/crash_processing/view_event_mutation.hpp"
#include "datadog/impl/rum/crash_processing/view_event_parser.hpp"
#include "datadog/impl/rum/rum.hpp"

namespace datadog::impl {

/**
 * Helper used to generate pre-JSON-encoded RUM events in response to crash reports,
 * bypassing all tracking consent checks in the current SDK instance.
 */
static bool produce_event_for_crash(
    const CrashContext& ctx,
    const EventWriter& event_writer,
    std::string_view event_data
) {
  // Sanity-check: because we bypass tracking consent, we should have already rejected
  // any crashes that occurred when the SDK did not have explicit tracking consent
  const bool bypass_tracking_consent = true;
  DATADOG_ASSERT(
      ctx.tracking_consent == TrackingConsent::Granted,
      "attempted to produce an event for a crash without tracking consent"
  );
  return event_writer(event_data, {}, bypass_tracking_consent);
}

/**
 * Helper used to convert a string device type (e.g. "desktop") to the corresponding
 * RUM-event-specific enum value (e.g. RumDeviceType::Desktop).
 */
static bool parse_rum_device_type(std::string_view value, RumDeviceType& out_type) {
  // Use the enum-value-to-string-name mapping established by DATADOG_STRING_ENUM to
  // perform a lookup
  for (const auto& enum_value : RumDeviceTypeValues) {
    if (enum_value.name == value) {
      // DATADOG_STRING_ENUM has static asserts to validate that `i` corresponds to a
      // valid enum value
      out_type = static_cast<RumDeviceType>(enum_value.i);
      return true;
    }
  }
  return false;
}

/**
 * Given a RumViewEvent or RumErrorEvent, populates the set of fields that correspond to
 * data from the previous process that's been stored in CrashContext.
 */
template <typename T>
static void populate_event_from_crash_context(const CrashContext& ctx, T& mut_ev) {
  // Convey basic details describing the SDK at the time of the crash, from
  // CrashContext, to the new RUM Error event
  mut_ev.service = ctx.service;
  mut_ev.version = ctx.application_version;

  // TODO: Include ddtags, built from ctx?

  // If CrashContext includes values for any requred 'os' fields, populate 'os'
  if (!ctx.os_name.empty() || !ctx.os_version.empty() ||
      !ctx.os_version_major.empty()) {
    mut_ev.os.value.emplace(ctx.os_name, ctx.os_version, ctx.os_version_major);
    mut_ev.os.value->build = ctx.os_build;
  }

  // If CrashContext includes any valid device details, populate 'device'
  RumDeviceType rum_device_type{};
  bool has_rum_device_type = parse_rum_device_type(ctx.device_type, rum_device_type);
  if (has_rum_device_type || !ctx.device_name.empty() || !ctx.device_model.empty() ||
      !ctx.device_brand.empty() || !ctx.device_architecture.empty() ||
      !ctx.device_locale.empty() || !ctx.device_time_zone.empty()) {
    mut_ev.device.value.emplace();
    if (has_rum_device_type) {
      mut_ev.device.value->type = rum_device_type;
    }
    mut_ev.device.value->name = ctx.device_name;
    mut_ev.device.value->model = ctx.device_model;
    mut_ev.device.value->brand = ctx.device_brand;
    mut_ev.device.value->architecture = ctx.device_architecture;
    mut_ev.device.value->locale = ctx.device_locale;
    mut_ev.device.value->time_zone = ctx.device_time_zone;
  }

  // If CrashContext includes any valid user details, populate 'usr'
  if (!ctx.user_id.empty() || !ctx.user_name.empty() || !ctx.user_email.empty() ||
      ctx.user_extra.GetObjectPropertyCount() > 0) {
    mut_ev.usr.value.emplace();
    mut_ev.usr.value->id = ctx.user_id;
    mut_ev.usr.value->name = ctx.user_name;
    mut_ev.usr.value->email = ctx.user_email;
    if (ctx.user_extra.GetObjectPropertyCount() > 0) {
      mut_ev.usr.value->extra = ctx.user_extra;
    }
  }

  // If CrashContext includes any valid account details, populate 'account'
  if (!ctx.account_id.empty() || !ctx.account_name.empty() ||
      ctx.account_extra.GetObjectPropertyCount() > 0) {
    mut_ev.account.value.emplace(ctx.account_id);
    mut_ev.account.value->name = ctx.account_name;
    if (ctx.account_extra.GetObjectPropertyCount() > 0) {
      // TODO: mut_ev.account.value->extra = ctx.account_extra;
    }
  }

  // If CrashContext contains a non-empty set of Global RUM Attributes at the time of
  // the crash, use those values in the 'context' field for our error event
  // TODO(RUM-15994): Use product of merging global RUM attributes into existing view
  // events?
  if (ctx.global_rum_attributes.GetObjectPropertyCount() > 0) {
    mut_ev.context = ctx.global_rum_attributes;
  }
}

/**
 * Given a RumErrorEvent that's been populated with a basic set of required fields,
 * populates additional fields to describe the provided CrashReport.
 */
static void populate_error_event_for_crash(
    const CrashReport& crash, const CrashContext& ctx, RumErrorEvent& mut_ev
) {
  // Set basic properties that describe all crashes
  mut_ev.error.is_crash = true;

  // The caller initializes the required property error.message to an empty string;
  // we're responsible for filling it in based on the details of the crash
  DATADOG_ASSERT(mut_ev.error.message.empty(), "RumErrorEvent has non-empty message");
  mut_ev.error.message = FormatCrashReportErrorMessage(crash);

  // Set error.source_type based on the platform for which the SDK is compiled:
  // in-process crash reports are handled on the same machine that wrote them
#ifdef _WIN32
  mut_ev.error.source_type = RumErrorSourceType::Windows;
#else
#ifdef __APPLE__
  mut_ev.error.source_type = RumErrorSourceType::MacOS;
#else
  mut_ev.error.source_type = RumErrorSourceType::Linux;
#endif
#endif

  // Build a multi-line string that encodes our stack trace, and store it in
  // error.stack: the expected format varies based on error.stack_trace, but all three
  // supported desktop platforms use the same "native" format
  mut_ev.error.stack = FormatCrashReportStack(crash);

  // Choose an appropriate CPU architecture value. The JSON schema implies that each
  // entry in error.binary_images[] specifies its own CPU arch (allowing multi-arch
  // environments like Rosetta on macOS), but in practice: the frontend grabs
  // error.binary_images[0].arch and supplies it in the symbolication request as
  // `device_arch`, so whatever we set in binary_images[0].arch applies to the entire
  // symbolication process for this crash.
  const std::string_view device_arch = DATADOG_BUILD_ARCH;

  // Address ranges given in error.binary_images[] are flexible: on the backend, they're
  // handled with parseAddress, which:
  // - accepts an empty string as a sentinel for zero
  // - accepts but does not require a '0x' (or '0X') prefix
  // - does not require any zero-padding or any specific case
  // For simplicity, we'll format these addresses as compactly as possible, using a
  // temporary buffer to build the strings that will be copied into each BinaryImage.
  std::array<char, 16> address_buf{};
  auto format_address = [&address_buf](uint64_t value) -> std::string_view {
    auto res = std::to_chars(
        address_buf.data(), address_buf.data() + address_buf.size(), value, 16
    );
    DATADOG_ASSERT(res.ec == std::errc{}, "failed hex formatting with 16-char buffer");
    const size_t size = res.ptr - address_buf.data();
    return std::string_view{address_buf.data(), size};
  };

  // Convey the list of loaded modules (a.k.a. binary images), which has already
  // been filtered down to to those that appear in the stack trace
  mut_ev.error.binary_images.value.reserve(crash.modules.size());
  for (const auto& module : crash.modules) {
    // Construct an error.binary_images[] entry with basic info
    auto& binary_image = mut_ev.error.binary_images.value.emplace_back(
        module.build_id, module.name, module.is_system
    );

    // error.binary_images[].load_address and max_address are specified as strings:
    // format them as hex values (lowercase, no prefix, no padding) and copy those
    // values into the struct's std::string members. Note that max_address, contrary to
    // what the name might imply, is an _exclusive_ upper bound; i.e. it's one byte past
    // the final byte that belongs to the module, same as module.end_address.
    binary_image.load_address = format_address(module.start_address);
    binary_image.max_address = format_address(module.end_address);

    // The crash reporter doesn't currently store an architecture value per-module; so
    // we just set error.binary_images[].arch to the same value, assuming that every
    // module shares our host architecture
    binary_image.arch = device_arch;
  }

  // Set common properties like 'usr', 'account', 'os', 'device', etc. from CrashContext
  populate_event_from_crash_context(ctx, mut_ev);
}

/**
 * Creates and returns an event describing an "ApplicationLaunch" view in which a single
 * crash has occurred.
 */
static RumViewEvent create_application_launch_view_for_crash(
    const UUID& application_id,
    const UUID& session_id,
    RumSessionType session_type,
    bool session_has_replay,
    const CrashReport& crash,
    const CrashContext& ctx
) {
  // We're creating a RUM View to record a crash: there is no user activity captured in
  // this view, and it will receive no further updates. Therefore:
  // - The view event's timestamp reflects the time of the crash
  // - We set an arbitrary view duration of 1ns
  const Timestamp date{std::chrono::milliseconds(crash.timestamp_ms)};
  const uint64_t view_time_spent_ns = 1;

  // Generate a random UUID to identify this view, and use a key that identifies it as
  // a synthetic view created prior to the existence of any other views
  const UUID view_id = UUID::Random();
  const std::string_view view_url = "com/datadog/application-launch/view";
  const std::string_view view_name = "ApplicationLaunch";

  // Initialize our view event with all required fields: this is the one and only event
  // that we'll send for this view, so it must already describe a view in which the
  // crash has occurred, hence setting error.count to 1
  const uint64_t view_action_count = 0;
  const uint64_t view_error_count = 1;
  const uint64_t view_resource_count = 0;
  const uint64_t internal_document_version = 1;
  RumViewEvent ev{
      date,
      application_id,
      session_id,
      session_type,
      view_id,
      view_url,
      view_time_spent_ns,
      view_action_count,
      view_error_count,
      view_resource_count,
      internal_document_version
  };

  // If we're creating this synthetic view in a session that originated in the crashed
  // process, and if that session was recorded with session replay, ensure that the UI
  // surfaces that recording in the context of this view
  ev.session.has_replay = session_has_replay;

  // Set view.name to "ApplicationLaunch"
  ev.view.name = view_name;

  // The view is dead on arrival; it will receive no further updates
  ev.view.is_active = false;

  // Set crash.count to 1
  ev.view.crash.value.emplace(1);

  // Set common properties like 'usr', 'account', 'os', 'device', etc. from CrashContext
  populate_event_from_crash_context(ctx, ev);

  return ev;
}

/**
 * Handles a crash that occurred before the SDK established any RUM session whatsoever.
 */
static void handle_crash_that_preceded_initial_session(
    RumScopeDependencies& deps,
    const CrashReport& crash,
    const CrashContext& ctx,
    const EventWriter& event_writer
) {
  // This function is only called for a specific subset of crashes
  DATADOG_ASSERT(
      ctx.rum_session_state.session_id == UUID::Zero,
      "crash that purportedly preceded initial session has nonzero session_id"
  );

  // There was no session active at the time of the crash, and a RUM Error must be
  // recorded in a session: generate a session_id and make a sampling decision
  // TODO(RUM-16002): Pass new_session_id to ShouldSampleSession
  const UUID new_session_id = UUID::Random();
  if (!deps.ShouldSampleSession()) {
    deps.diagnostic_logger.Status(
        "Ignoring prior-process crash report: newly-created session was excluded from "
        "sampling"
    );
    return;
  }

  // We always assume this is an ordinary user session, as we don't yet support ci-test
  // or synthetics integrations
  const RumSessionType session_type = RumSessionType::User;

  // There will never be Session Replay data for this session, as it's being created
  // solely to contain the crash
  const bool session_has_replay = false;

  // Generate an event describing a new ApplicationLaunch view to contain our crash
  RumViewEvent view_ev = create_application_launch_view_for_crash(
      deps.application_id, new_session_id, session_type, session_has_replay, crash, ctx
  );
  produce_event_for_crash(ctx, event_writer, deps.EncodeEvent(view_ev));

  // Generate a RUM Error event that reflects the details of our synthetic view
  RumErrorEvent ev(
      Timestamp{std::chrono::milliseconds(crash.timestamp_ms)},
      deps.application_id,
      new_session_id,
      session_type,
      view_ev.view.id,
      view_ev.view.url,
      "",
      RumErrorSource::Source
  );
  ev.error.id = UUID::Random();
  ev.session.has_replay = session_has_replay;
  ev.view.name = view_ev.view.name;
  populate_error_event_for_crash(crash, ctx, ev);
  produce_event_for_crash(ctx, event_writer, deps.EncodeEvent(ev));

  // Log a status message to indicate that we successfully produced RUM events in
  // response to a crash report
  deps.diagnostic_logger.Status(
      "Handled crash report: created new session, created ApplicationLaunch view, and "
      "recorded RUM Error",
      {{"session_id", new_session_id},
       {"view_id", view_ev.view.id},
       {"error_id", ev.error.id.value},
       {"error_message", ev.error.message}}
  );
}

/**
 * Handles a crash that occurred while the initial RUM session was active and sampled,
 * but before any RUM views had been created in that session.
 */
static void handle_crash_that_preceded_initial_view_in_initial_session(
    RumScopeDependencies& deps,
    const CrashReport& crash,
    const CrashContext& ctx,
    const EventWriter& event_writer
) {
  // This function is only called for a specific subset of crashes
  DATADOG_ASSERT(
      ctx.rum_session_state.session_id != UUID::Zero,
      "crash in active session has nil session_id"
  );
  DATADOG_ASSERT(
      ctx.last_view_event_json.empty(),
      "crash before initial view has non-empty last view event"
  );
  DATADOG_ASSERT(
      ctx.rum_session_state.is_initial_session, "crash did not occur in initial session"
  );
  DATADOG_ASSERT(
      !ctx.rum_session_state.has_tracked_any_view,
      "crash did not occur in session with no tracked views"
  );

  // This logic is essentially the same as handle_crash_that_preceded_initial_session,
  // except that instead of synthesizing a new session and making a sampling decision,
  // we create the ApplicationLaunch view and error in the context of an
  // already-existing session from the process that crashed
  DATADOG_ASSERT(
      ctx.rum_session_state.is_sampled,
      "attempting to create events for prior-process session that was not sampled"
  );

  // We always assume this is an ordinary user session, as we don't yet support ci-test
  // or synthetics integrations
  const RumSessionType session_type = RumSessionType::User;

  // If the session where the crash occurred was being recorded with Session Replay,
  // ensure that session.has_replay is true so that the Datadog UI will link the user
  // from this view/error to the replay
  const bool session_has_replay = ctx.rum_session_state.did_start_with_replay;

  // Generate an event describing a new ApplicationLaunch view to contain our crash
  RumViewEvent view_ev = create_application_launch_view_for_crash(
      deps.application_id,
      ctx.rum_session_state.session_id,
      session_type,
      session_has_replay,
      crash,
      ctx
  );
  produce_event_for_crash(ctx, event_writer, deps.EncodeEvent(view_ev));

  // Generate a RUM Error event that reflects the details of our synthetic view
  RumErrorEvent ev(
      Timestamp{std::chrono::milliseconds(crash.timestamp_ms)},
      deps.application_id,
      ctx.rum_session_state.session_id,
      session_type,
      view_ev.view.id,
      view_ev.view.url,
      "",
      RumErrorSource::Source
  );
  ev.error.id = UUID::Random();
  ev.session.has_replay = session_has_replay;
  ev.view.name = view_ev.view.name;
  populate_error_event_for_crash(crash, ctx, ev);
  produce_event_for_crash(ctx, event_writer, deps.EncodeEvent(ev));

  // Log a status message to indicate that we successfully produced RUM events in
  // response to a crash report
  deps.diagnostic_logger.Status(
      "Handled crash report: created ApplicationLaunch view and recorded RUM Error in "
      "prior-process session",
      {{"session_id", ctx.rum_session_state.session_id},
       {"view_id", view_ev.view.id},
       {"error_id", ev.error.id.value},
       {"error_message", ev.error.message}}
  );
}

/**
 * Handles a crash that occurred during an active RUM session while a RUM view was
 * active.
 */
static void handle_crash_that_had_active_view(
    RumScopeDependencies& deps,
    const CrashReport& crash,
    const CrashContext& ctx,
    const EventWriter& event_writer
) {
  // This function is only called for a specific subset of crashes
  DATADOG_ASSERT(
      ctx.rum_session_state.session_id != UUID::Zero,
      "crash in active session has nil session_id"
  );
  DATADOG_ASSERT(
      !ctx.last_view_event_json.empty(), "crash in active view has no last view event"
  );

  // Perform minimal parsing of the last RUM View event produced before the crash, to
  // obtain the details of that view
  RumViewEventParser parser;
  if (!parser.Parse(ctx.last_view_event_json)) {
    // Failure to parse indicates that the prior run of the SDK may have written a
    // malformed RUM view event in the serialized CrashContext
    deps.diagnostic_logger.Warning(
        "Failed to handle prior-process crash: last view event could not be parsed"
    );
    return;
  }

  // Determine whether the crash took place within the past 4 hours: if it's older, we
  // assume that the backend will not accept newly-arrived view events for such an old
  // view; but for a view that's still able to be updated we want to send a RUM View
  // event that updates the state of the view to reflect the crash
  const Timestamp current_time = deps.clock.Now();
  const Timestamp view_update_cutoff = current_time - std::chrono::hours(4);
  const uint64_t crash_timestamp_ms = crash.timestamp_ms;
  auto crash_timestamp = Timestamp{std::chrono::milliseconds(crash_timestamp_ms)};
  const bool send_updated_view_event = crash_timestamp >= view_update_cutoff;
  if (send_updated_view_event) {
    // If we have a non-empty snapshot of the set of global RUM attributes that were
    // present at the time of the crash, encode them as JSON so we can replace any
    // existing 'context' object on the view event with a more up-to-date snapshot.
    // TODO(RUM-15994): This clobbers existing view-level attributes, consistent with
    // what the iOS SDK does. We may instead want to merge the CrashContext's global
    // attributes with the existing `context` value from the last view event, and then
    // also carry that merged value forward to RumErrorEvent::context.
    std::string_view attributes_json{};
    std::vector<uint8_t> encode_buf;
    if (ctx.global_rum_attributes.GetObjectPropertyCount() > 0) {
      EncodeJson(encode_buf, ctx.global_rum_attributes);
      attributes_json = std::string_view{
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<char*>(encode_buf.data()),
          encode_buf.size()
      };
    }

    // We've successfully parsed the last RUM view event, so modifying a few fields to
    // produce a new JSON view event is a straightforward operation
    std::string new_view_event = MutateViewEventForCrash(
        ctx.last_view_event_json,
        parser.spans,
        parser.values,
        crash_timestamp_ms,
        attributes_json
    );
    DATADOG_ASSERT(
        !new_view_event.empty(), "MutateViewEventForCrash returned an empty result"
    );
    produce_event_for_crash(ctx, event_writer, new_view_event);
  } else {
    // View is too old to be updated: log timing details for diagnostics
    deps.diagnostic_logger.Debug(
        "Sending no view update for prior-process crash that had active view: cutoff "
        "time has passed",
        {{"current_time", current_time},
         {"crash_time", crash_timestamp},
         {"cutoff_time", view_update_cutoff}}
    );
  }

  // Regardless of whether we produced an updated view event, prepare a RUM Error event
  // to describe the crash in the context of that view, carrying over any relevant
  // details that were parsed from the View event
  RumErrorEvent ev(
      crash_timestamp,
      deps.application_id,
      ctx.rum_session_state.session_id,
      parser.values.session_type,
      parser.values.view_id,
      parser.values.view_url,
      "",
      RumErrorSource::Source
  );
  ev.error.id = UUID::Random();
  if (!parser.values.build_version.empty()) {
    ev.build_version = parser.values.build_version;
  }
  if (!parser.values.build_id.empty()) {
    ev.build_id = parser.values.build_id;
  }
  if (parser.values.session_has_replay) {
    ev.session.has_replay = true;
  }
  if (!parser.values.view_name.empty()) {
    ev.view.name = parser.values.view_name;
  }

  // Further populate the RumErrorEvent with all the necessary data to describe our
  // crash, including the stack trace and loaded modules from the crash, as well as all
  // the relevant data from CrashContext
  populate_error_event_for_crash(crash, ctx, ev);

  // Serialize our RumErrorEvent as JSON and enqueue it for storage alongside all other
  // RUM view events generated by this SDK instance, bypassing tracking consent
  produce_event_for_crash(ctx, event_writer, deps.EncodeEvent(ev));

  // Log a status message to indicate that we successfully produced RUM events in
  // response to a crash report
  deps.diagnostic_logger.Status(
      "Handled crash report: recorded RUM Error in prior-process view",
      {{"session_id", ctx.rum_session_state.session_id},
       {"view_id", parser.values.view_id},
       {"view_updated", send_updated_view_event},
       {"error_id", ev.error.id.value},
       {"error_message", ev.error.message}}
  );
}

void ContextThread_HandleCrashReport(
    RumScopeDependencies& deps,
    const CrashReport& crash,
    const EventWriter& event_writer
) {
  // If the crash report does not have an intact CrashContext describing the RUM state,
  // tracking consent, etc. at the time of the crash, we're unable to handle this crash
  if (!crash.context.has_value()) {
    // This could indicate that a context file was missing or unreadable due to:
    //   - Filesystem errors beyond our control
    //   - External tampering beyond our control
    //   - Excessively long string/array/object values that caused us to reject the file
    //     as malformed
    // Or it could indicate that we never wrote a context file, due to:
    //   - A bug that's broken the SDK's context-handling or context-writing code
    //   - A crash that occurred very early in the process, before the SDK started or
    //     before CrashReporting had a chance to handle the initial
    //     `ContextChangedMessage` by flushing an initial context file
    deps.diagnostic_logger.Warning(
        "Ignoring prior-process crash report due to missing context"
    );
    return;
  }
  const CrashContext& ctx = *crash.context;

  // RUM's crash-processing logic bypasses the ordinary mechanism for handling tracking
  // consent on event writes: instead, we generate events for a crash iff tracking
  // consent was granted at the time the crash occurred, unconditionally storing those
  // events for upload. If we didn't have consent when this crash occurred, silently
  // drop it.
  if (ctx.tracking_consent != TrackingConsent::Granted) {
    // Note that we drop the crash report even if tracking consent was pending: this is
    // consistent with the behavior of the iOS SDK
    deps.diagnostic_logger.Status(
        "Ignoring prior-process crash report due to lack of tracking consent at time "
        "of crash",
        {{"previous_consent",
          ctx.tracking_consent == TrackingConsent::NotGranted ? "not-granted"
                                                              : "pending"}}
    );
    return;
  }

  // If the crash occurred while a RUM session had already been established, check the
  // details of the most-recently-active session to see if we should ignore this crash
  if (ctx.rum_session_state.session_id != UUID::Zero) {
    // If a session was active, but was excluded from sampling due to a <100% session
    // sampling rate, we've already committed to never sending events for that session
    if (!ctx.rum_session_state.is_sampled) {
      deps.diagnostic_logger.Status(
          "Ignoring prior-process crash report: crash occurred during a session that "
          "was not sampled",
          {{"session_id", ctx.rum_session_state.session_id}}
      );
      return;
    }

    // If ctx.rum_session_state.is_active is false, the application called StopSession
    // to explicitly end tracking prior to the crash. Consistent with the behavior of
    // the iOS SDK, we don't handle this case any differently: it'll fall through to the
    // checks below, giving crashes that occurred after StopSession a chance to be
    // handled.
  }

  // We have a valid CrashContext, we had tracking consent at the time of the crash, and
  // the crash did not occur in a non-sampled session: therefore, we should attempt to
  // handle the crash.

  // We report crashes as RUM Errors, and an Error must be recorded in the context of a
  // RUM View, which itself belongs to a RUM Session. Therefore, depending on RUM state
  // at the time of the crash, we may need to synthesize a session, synthesize a view,
  // and/or update an existing view; in addition to recording a RUM Error.

  // We can take one of three major branches:

  // 1. If no RUM session had been established at the time of the crash, we generate a
  //    new session, with a random UUID, using our currently-configured application_id,
  //    and then we make a new sampling decision for that session based on our current
  //    configuration. Based on the results of that sampling decision, we either:
  //     a.) Ignore the crash (when the new session is not sampled), or
  //     b.) Synthesize an `ApplicationLaunch` view for the new session, then generate
  //         both a RUM View event and a RUM Error event
  if (ctx.rum_session_state.session_id == UUID::Zero) {
    handle_crash_that_preceded_initial_session(deps, crash, ctx, event_writer);
    return;
  }

  // 2. If the crash occurred with an active session but no active view, we branch based
  //    on RUM session state.
  if (ctx.last_view_event_json.empty()) {
    // 2a. If the crash occurred during the very first RUM session created, before any
    //     view had been tracked, we can synthesize an `ApplicationLaunch` view within
    //     that session, generating both a RUM View event and a RUM Error event.
    const bool can_create_application_launch_view =
        ctx.rum_session_state.is_initial_session &&
        !ctx.rum_session_state.has_tracked_any_view;
    if (can_create_application_launch_view) {
      handle_crash_that_preceded_initial_view_in_initial_session(
          deps, crash, ctx, event_writer
      );
      return;
    }

    // 2b. Otherwise, the crash is considered an "off-view" or "Background" event, which
    //     we don't currently handle.
    // TODO(RUM-12247): Create a synthetic `Background` view to track the crash, and
    // generate a RUM View event and RUM Error event
    deps.diagnostic_logger.Warning(
        "Ignoring prior-process crash report: crash occurred while no RUM View was "
        "active"
    );
    return;
  }

  // 3. Otherwise, the crash had an active view which belonged to an active, sampled-in
  //    session. In this case, we do two things in sequence:
  //    i. If the crash is less than 4 hours old, we mutate the last view event such
  //       that it describes the final state of the view, recording that the view was
  //       ended by the crash.
  //   ii. Unconditionally, we produce a RUM Error event that describes the crash,
  //       carrying over the requisite fields from the last view event such that the
  //       Error is recorded in the context of that View.
  handle_crash_that_had_active_view(deps, crash, ctx, event_writer);
}

}  // namespace datadog::impl
