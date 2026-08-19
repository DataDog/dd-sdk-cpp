// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string>
#include <string_view>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/json/json_scanner.hpp"

namespace datadog::impl {

/**
 * Limited JSON parser purpose-built for parsing RUM View events within RUM's
 * crash-processing code.
 *
 * The SDK does not need general-purpose JSON parsing code: we only need to examine a
 * subset of fields on a JSON-encoded RumViewEvent when processing in-process crash
 * reports in the RUM implementation, and in some cases that same code needs to mutate a
 * few of those fields in order to produce an updated view event.
 *
 * While we could use a third-party JSON library to handle this parsing and mutation,
 * there are a number of downsides that make that option unappealing, including:
 *
 * - Impact on binary size
 * - Potential runtime inefficiency
 * - Security surface area added by a transitive third-party dependency
 * - Compatibility concerns on platforms that the third-party library may not support
 * - Potential ODR violations and other headaches when integrating the SDK into an
 *   application that links against the same third-party library
 *
 * This parser takes advantage of the fact that we only ever need to parse RUM View
 * events produced by the SDK itself, allowing us to make well-informed assumptions
 * about the format of the JSON data and meet all of our JSON-parsing needs with a few
 * hundred lines of standard C++.
 *
 * No allocations are performed during parsing, except for a small handful of
 * `std::string` values that we may need to allocate in order to store unescaped
 * versions of string literals.
 */
struct RumViewEventParser {
  using Span = JsonScanner::Span;

  /**
   * Attempts to parse a JSON object literal that was serialized by this SDK from a
   * RumViewEvent struct. If successful, populates `spans` and `values` with all
   * required data.
   */
  bool Parse(std::string_view view_event_json);

  /**
   * Positions in the input JSON string where each of these values has been located.
   *
   * After Parse() succeeds, all required values will have a valid Span, and the
   * presence of optional values can be checked with Span::OK(). A valid span indicates
   * that `RumViewEventParser::Parse()` found a literal JSON value conforming to the
   * expected type at the associated path in the input string.
   */
  struct Spans {
    Span date;                    // date
    Span application_id;          // application.id
    Span build_version;           // build_version (optional)
    Span build_id;                // build_id (optional)
    Span session_id;              // session.id
    Span session_type;            // session.type
    Span session_has_replay;      // session.has_replay (optional)
    Span view_id;                 // view.id
    Span view_url;                // view.url
    Span view_name;               // view.name (optional)
    Span view_is_active;          // view.is_active
    Span view_error_count;        // view.error.count
    size_t view_error_end_pos{};  // (index of comma after view.error object value,
                                  //  where we'll insert a new view.crash object)
    Span dd_format_version;       // _dd.format_version
    Span dd_document_version;     // _dd.document_version
    size_t dd_end_pos{};          // (index of comma after _dd object value, where we'll
                                  //  insert context if no existing context object)
    Span context;                 // context
    Span type;                    // type

    /**
     * Returns true if `Spans` contains valid matches for all required values.
     *
     * Used for validation in `RumViewEventParser::Parse()`; defined here to remain as
     * close as possible to the declaration of `Spans`.
     */
    bool HasAllRequiredMatches() const {
      // Note that view.is_active is technically not required in the rum-events-format
      // schema, but in practice, an SDK never emits a RUM View event without an
      // explicit value for view.is_active
      return date.OK() && application_id.OK() && session_id.OK() && session_type.OK() &&
             view_id.OK() && view_url.OK() && view_is_active.OK() &&
             view_error_count.OK() && view_error_end_pos != 0 &&
             dd_format_version.OK() && dd_document_version.OK() && dd_end_pos != 0 &&
             type.OK();
    }

    /**
     * Returns true if all values whose ranges we stored in `Spans` appear in the input
     * string in the order we expect them to, based on the declaration order in
     * DATADOG_JSON_STRUCT(RumViewEvent, ...), without overlap.
     *
     * While it's not strictly necessary to validate field ordering to this extent, it's
     * a useful sanity check to ensure that parsing has truly succeeded and that the
     * event we're processing was actually produced by the SDK, as it's our expectation
     * that the only RUM View events we'll be parsing are those that were left on disk
     * by the SDK.
     *
     * Used for validation in `RumViewEventParser::Parse()`; defined here to remain as
     * close as possible to the declaration of `Spans`.
     */
    bool HasExpectedFieldOrdering() const {
      // Prepare to iterate through all spans, updating prev_end to the position where
      // the last value ended, so we can verify that all properties are present in the
      // order we expect (as declared in DATADOG_JSON_STRUCT for RumViewEvent)
      size_t prev_end = 0;

      // For required properties, verify that they appear later in the string than the
      // last value we checked, updating prev_end on success or else returning false
      auto validate_required = [&](const Span& span) -> bool {
        if (span.i < prev_end) {
          return false;
        }
        prev_end = span.i + span.len;
        return true;
      };

      // For optional properties, apply the same validation logic but only if a matching
      // value was found
      auto validate_optional = [&](const Span& span) -> bool {
        if (span.OK()) {
          if (span.i < prev_end) {
            return false;
          }
          prev_end = span.i + span.len;
        }
        return true;
      };

      // For required indices indicating positions where new values may be inserted, use
      // the same logic as validate_required, treating these as single-char spans
      auto validate_required_pos = [&](const size_t pos) -> bool {
        if (pos < prev_end) {
          return false;
        }
        prev_end = pos;
        return true;
      };

      // Require that the JSON payload we've parsed has all fields in the same order in
      // which they're declared in DATADOG_JSON_STRUCT(RumViewEvent, ...)
      // clang-format off
      if (!validate_required(date)) { return false; }
      if (!validate_required(application_id)) { return false; }
      if (!validate_optional(build_version)) { return false; }
      if (!validate_optional(build_id)) { return false; }
      if (!validate_required(session_id)) { return false; }
      if (!validate_required(session_type)) { return false; }
      if (!validate_optional(session_has_replay)) { return false; }
      if (!validate_required(view_id)) { return false; }
      if (!validate_required(view_url)) { return false; }
      if (!validate_optional(view_name)) { return false; }
      if (!validate_required(view_is_active)) { return false; }
      if (!validate_required(view_error_count)) { return false; }
      if (!validate_required_pos(view_error_end_pos)) { return false; }
      if (!validate_required(dd_format_version)) { return false; }
      if (!validate_required(dd_document_version)) { return false; }
      if (!validate_required_pos(dd_end_pos)) { return false; }
      if (!validate_optional(context)) { return false; }
      if (!validate_required(type)) { return false; }
      // clang-format on

      // Field ordering is as we expect; it's safe to mutate the value with careful
      // string slicing
      return true;
    }
  };
  Spans spans;

  /**
   * Values parsed from the input string, from the substring ranges in `spans`.
   *
   * After Parse() succeeds, all values that were identified in `spans` will be set.
   */
  struct Values {
    std::string build_version;       // build_version (optional; empty if not set)
    std::string build_id;            // build_id (optional; empty if not set)
    UUID application_id;             // application.id
    UUID session_id;                 // session.id
    UUID view_id;                    // view.id
    RumSessionType session_type{};   // session.type
    bool session_has_replay{};       // session.has_replay (optional; false if not set)
    std::string view_url;            // view.url
    std::string view_name;           // view.name (optional; empty if not set)
    uint64_t view_error_count{};     // view.error.count
    uint64_t dd_format_version{};    // _dd.format_version
    uint64_t dd_document_version{};  // _dd.document_version
  };
  Values values;

 private:
  // Helper functions for traversing JSON objects to identify the substrings
  // representing literal values, whose ranges will be stored in `spans`
  void ScanRootObject(JsonScanner& scanner);
  void ScanApplicationObject(JsonScanner& scanner);
  void ScanSessionObject(JsonScanner& scanner);
  void ScanViewObject(JsonScanner& scanner);
  static void ScanViewIncidenceCount(JsonScanner& scanner, Span& out_count_span);
  void ScanInternalObject(JsonScanner& scanner);

  // Helper functions for parsing those JSON literals into `values`
  static bool ParseUUID(std::string_view json_literal, UUID& out_value);
  static bool ParseString(std::string_view json_literal, std::string& out_value);
  static bool ParseUInt64(std::string_view json_literal, uint64_t& out_value);
  static bool ParseBool(std::string_view json_literal, bool& out_value);
  static bool ParseRumSessionType(
      std::string_view json_literal, RumSessionType& out_value
  );
};

}  // namespace datadog::impl
