// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_processing/view_event_parser.hpp"

#include "datadog/impl/types/json/parse_primitives.hpp"

namespace datadog::impl {

bool RumViewEventParser::Parse(std::string_view view_event_json) {
  // Ensure that no leftover results remain if a parser is reused for multiple events
  spans = Spans{};
  values = Values{};

  // Use a JsonScanner to traverse the top-level object and its subobjects, identifying
  // property values at specific paths and storing the ranges of each JSON literal value
  // in the input string in `spans`
  JsonScanner scanner{view_event_json};
  ScanRootObject(scanner);

  // If the input value was not valid JSON or did not match the expected shape, fail
  if (!scanner.OK()) {
    return false;
  }

  // If we failed to identify values for all required properties, fail: these are
  // properties that are mandatory according to the JSON schema and are therefore
  // declared as non-Omissible in RumViewEvent, so if we failed to parse them, we don't
  // have a valid view event
  if (!spans.HasAllRequiredMatches()) {
    return false;
  }

  // If we need to update the view, we'll produce a new view event by slicing the
  // original event data to perform our mutations: this is made easier and safer if we
  // can guarantee that the event we're dealing with has its properties in the canonical
  // order that we expect the SDK to produce
  if (!spans.HasExpectedFieldOrdering()) {
    return false;
  }

  // We've identified the valid JSON literals for all required fields, and we have their
  // positions in the input string mapped out. There's a subset of these values that we
  // need to parse so that we can increment them, use their value in a RUM Error event,
  // or make decisions about how to handle a crash based on them: proceed to parsing
  // these values from the JSON literals we've identified.
  auto extract = [&view_event_json](JsonScanner::Span span) -> std::string_view {
    return view_event_json.substr(span.i, span.len);
  };

  // The top-level "type" value on any RUM view event should be "view", which we can
  // check as a quoted JSON literal since it has no escape sequences. Other strings
  // (view.url, view.name, etc.) may have escape sequences, so we must parse them into
  // separate std::string values for our use.
  if (extract(spans.type) != "\"view\"") {
    return false;
  }

  // If build_version and/or build_id are set, parse them so we can set the same values
  // in any RumErrorEvent that we produce
  if (spans.build_version.OK()) {
    if (!ParseJsonString(extract(spans.build_version), values.build_version)) {
      return false;
    }
  }
  if (spans.build_id.OK()) {
    if (!ParseJsonString(extract(spans.build_id), values.build_id)) {
      return false;
    }
  }

  // Any valid View event must have a nonzero UUID set for application.id and session.id
  if (!ParseJsonUUID(extract(spans.application_id), values.application_id) ||
      values.application_id == UUID::Zero) {
    return false;
  }
  if (!ParseJsonUUID(extract(spans.session_id), values.session_id) ||
      values.session_id == UUID::Zero) {
    return false;
  }

  // Extract the basic details of the session that contained the view: these values
  // (session.type, and session.has_replay if true) will be carried forward into
  // RumErrorEvent::Session
  if (!ParseRumSessionType(extract(spans.session_type), values.session_type)) {
    return false;
  }
  if (spans.session_has_replay.OK()) {
    if (!ParseJsonBool(extract(spans.session_has_replay), values.session_has_replay)) {
      return false;
    }
  }

  // Extract the basic details of the view: these same values (view.id, view.url, and
  // view.name if present) must be encoded in RumErrorEvent::View
  if (!ParseJsonUUID(extract(spans.view_id), values.view_id) ||
      values.view_id == UUID::Zero) {
    return false;
  }
  if (!ParseJsonString(extract(spans.view_url), values.view_url)) {
    return false;
  }
  if (spans.view_name.OK()) {
    if (!ParseJsonString(extract(spans.view_name), values.view_name)) {
      return false;
    }
  }

  // If we end up producing a new event for this view, we'll need to increment
  // view.error.count by one, so we need its current value (and we'll also need to
  // insert view.crash.count with a value of 1 right after it)
  if (!ParseJsonUInt64(extract(spans.view_error_count), values.view_error_count)) {
    return false;
  }

  // _dd.format_version should be 2, as it's hardcoded in the schema (see also
  // RumViewEvent::Internal), and this SDK has never supported values of any other
  // version. If _dd.format_version is ever incremented, this code will need to be
  // updated as well.
  if (!ParseJsonUInt64(extract(spans.dd_format_version), values.dd_format_version)) {
    return false;
  }
  if (values.dd_format_version != 2) {
    return false;
  }

  // _dd.document_version is a monotonic counter incremented with each new event
  // describing the same view: we'll need to increment it if we produce a new view event
  if (!ParseJsonUInt64(
          extract(spans.dd_document_version), values.dd_document_version
      )) {
    return false;
  }

  // We've successfully parsed all values: this is a valid RUM View event
  return true;
}

void RumViewEventParser::ScanRootObject(JsonScanner& scanner) {
  // We expect an object with top-level properties arranged like so:
  // {"date":946684799999,"application":{...},"session":{...},"view":{...},"_dd":{...},"type":"view"}
  if (scanner.EnterObject()) {
    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("date")) {
        spans.date = scanner.SkipNumberLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("application")) {
        ScanApplicationObject(scanner);
      } else if (scanner.TrySkipObjectPropertyKey("build_version")) {
        spans.build_version = scanner.SkipStringLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("build_id")) {
        spans.build_id = scanner.SkipStringLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("session")) {
        ScanSessionObject(scanner);
      } else if (scanner.TrySkipObjectPropertyKey("view")) {
        ScanViewObject(scanner);
      } else if (scanner.TrySkipObjectPropertyKey("_dd")) {
        ScanInternalObject(scanner);

        // In the event that there's no 'context' value in the original view event, we
        // may need to insert a new property: store the position of the comma that
        // follows `"_dd":{...}` so we can insert it at that point if needed
        if (scanner.Peek() == ',') {
          // We should always have a comma after _dd in all events produced by the SDK,
          // even if context is not present, because 'type' follows '_dd' and both are
          // required
          spans.dd_end_pos = scanner.pos;
        } else {
          scanner.Fail();
        }
      } else if (scanner.TrySkipObjectPropertyKey("context")) {
        // In the event that a context value _is_ found, we'll replace it rather than
        // inserting a brand new value
        spans.context = scanner.SkipObjectLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("type")) {
        spans.type = scanner.SkipStringLiteral();
      } else {
        // Advance past properties that we don't need to handle
        scanner.SkipObjectProperty();
      }
      // Skip delimiter between properties
      scanner.SkipObjectPropertySeparator();
    }
    // Skip closing brace
    if (scanner.OK()) {
      scanner.Advance();
    }
  }
}

void RumViewEventParser::ScanApplicationObject(JsonScanner& scanner) {
  // We expect 'application' to contain:
  // {"id":"a991ca10-4004-4004-4004-beefbeefbeef",...}
  if (scanner.EnterObject()) {
    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("id")) {
        spans.application_id = scanner.SkipStringLiteral();
      } else {
        // Advance past properties that we don't need to handle
        scanner.SkipObjectProperty();
      }
      // Skip delimiter between properties
      scanner.SkipObjectPropertySeparator();
    }
    // Skip closing brace
    if (scanner.OK()) {
      scanner.Advance();
    }
  }
}

void RumViewEventParser::ScanSessionObject(JsonScanner& scanner) {
  // We expect 'session' to contain:
  // {"id":"5e551017-4114-4114-4114-beeeefbeeeef","type":"user"}
  if (scanner.EnterObject()) {
    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("id")) {
        spans.session_id = scanner.SkipStringLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("type")) {
        spans.session_type = scanner.SkipStringLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("has_replay")) {
        spans.session_has_replay = scanner.SkipBoolLiteral();
      } else {
        // Advance past properties that we don't need to handle
        scanner.SkipObjectProperty();
      }
      // Skip delimiter between properties
      scanner.SkipObjectPropertySeparator();
    }
    // Skip closing brace
    if (scanner.OK()) {
      scanner.Advance();
    }
  }
}

void RumViewEventParser::ScanViewObject(JsonScanner& scanner) {
  // We expect 'view' to contain:
  // {"id":"141ee144-4224-4224-4224-beeeeeeeeeef","url":"my-view","time_spent":42,"is_active":false,"action":{"count":3},"error":{"count":9},"resource":{"count":7}}
  if (scanner.EnterObject()) {
    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("id")) {
        spans.view_id = scanner.SkipStringLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("url")) {
        spans.view_url = scanner.SkipStringLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("name")) {
        spans.view_name = scanner.SkipStringLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("is_active")) {
        spans.view_is_active = scanner.SkipBoolLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("error")) {
        // We'll need to identify the position of the value for view.error.count so we
        // can increment it if we end up mutating this event
        ScanViewIncidenceCount(scanner, spans.view_error_count);

        // In addition to incrementing view.error.count, we'll also need to set
        // view.crash.count to 1, which will require inserting `,"crash":{"count":1}`
        // into the JSON data: we choose to do this after view.error, so store the
        // position of the comma that follows the object we just parsed
        if (scanner.Peek() == ',') {
          // We should always have a comma after view.error in all events produced by
          // the SDK, since view.error and view.resource (which follows it) are both
          // mandatory
          spans.view_error_end_pos = scanner.pos;
        } else {
          scanner.Fail();
        }
      } else if (scanner.TrySkipObjectPropertyKey("crash")) {
        // If we find an existing view.crash value, reject this view event outright
        // rather than attempting to increment view.crash.count from its current value:
        // we assume that a view can only experience a single crash, and that events
        // produced by the SDK under normal circumstances do not have view.crash set
        scanner.Fail();
      } else {
        // Advance past properties that we don't need to handle
        scanner.SkipObjectProperty();
      }
      // Skip delimiter between properties
      scanner.SkipObjectPropertySeparator();
    }
    // Skip closing brace
    if (scanner.OK()) {
      scanner.Advance();
    }
  }
}

void RumViewEventParser::ScanViewIncidenceCount(
    JsonScanner& scanner, Span& out_count_span
) {
  // For incidence objects like 'view.error' etc., we expect:
  // {"count":0}
  if (scanner.EnterObject()) {
    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("count")) {
        out_count_span = scanner.SkipNumberLiteral();
      } else {
        // Advance past properties that we don't need to handle
        scanner.SkipObjectProperty();
      }
      // Skip delimiter between properties
      scanner.SkipObjectPropertySeparator();
    }
    // Skip closing brace
    if (scanner.OK()) {
      scanner.Advance();
    }
  }
}

void RumViewEventParser::ScanInternalObject(JsonScanner& scanner) {
  // For the '_dd' object, i.e. RumViewEvent::Internal, we expect:
  // {"format_version":2,"document_version":5}
  if (scanner.EnterObject()) {
    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("format_version")) {
        spans.dd_format_version = scanner.SkipNumberLiteral();
      } else if (scanner.TrySkipObjectPropertyKey("document_version")) {
        spans.dd_document_version = scanner.SkipNumberLiteral();
      } else {
        // Advance past properties that we don't need to handle
        scanner.SkipObjectProperty();
      }
      // Skip delimiter between properties
      scanner.SkipObjectPropertySeparator();
    }
    // Skip closing brace
    if (scanner.OK()) {
      scanner.Advance();
    }
  }
}

bool RumViewEventParser::ParseRumSessionType(
    std::string_view json_literal, RumSessionType& out_value
) {
  // DATADOG_STRING_ENUM(StringRumSessionType, RumSessionType, ...) defines a limited
  // set of possible values; parse them from literal JSON strings, failing if
  // unrecognized
  if (json_literal == "\"user\"") {
    out_value = RumSessionType::User;
    return true;
  }
  if (json_literal == "\"synthetics\"") {
    out_value = RumSessionType::Synthetics;
    return true;
  }
  if (json_literal == "\"ci_test\"") {
    out_value = RumSessionType::CITest;
    return true;
  }
  // If RumSessionType is ever updated, this code will need to be updated as well
  return false;
}

}  // namespace datadog::impl
