// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/view_event_parser.hpp"

#include <charconv>

namespace datadog::impl {

bool RumViewEventParser::Parse(std::string_view view_event_json) {
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
    if (!ParseString(extract(spans.build_version), values.build_version)) {
      return false;
    }
  }
  if (spans.build_id.OK()) {
    if (!ParseString(extract(spans.build_id), values.build_id)) {
      return false;
    }
  }

  // Extract the basic details of the session that contained the view: these values
  // (session.type, and session.has_replay if true) will be carried forward into
  // RumErrorEvent::Session
  if (!ParseRumSessionType(extract(spans.session_type), values.session_type)) {
    return false;
  }
  if (spans.session_has_replay.OK()) {
    if (!ParseBool(extract(spans.session_has_replay), values.session_has_replay)) {
      return false;
    }
  }

  // Extract the basic details of the view: these same values (view.id, view.url, and
  // view.name if present) must be encoded in RumErrorEvent::View
  if (!ParseUUID(extract(spans.view_id), values.view_id)) {
    return false;
  }
  if (!ParseString(extract(spans.view_url), values.view_url)) {
    return false;
  }
  if (spans.view_name.OK()) {
    if (!ParseString(extract(spans.view_name), values.view_name)) {
      return false;
    }
  }

  // If we end up producing a new event for this view, we'll need to increment
  // view.error.count by one, so we need its current value (and we'll also need to
  // insert view.crash.count with a value of 1 right after it)
  if (!ParseUInt64(extract(spans.view_error_count), values.view_error_count)) {
    return false;
  }

  // _dd.format_version should be 2, as it's hardcoded in the schema (see also
  // RumViewEvent::Internal), and this SDK has never supported values of any other
  // version. If _dd.format_version is ever incremented, this code will need to be
  // updated as well.
  if (!ParseUInt64(extract(spans.dd_format_version), values.dd_format_version)) {
    return false;
  }
  if (values.dd_format_version != 2) {
    return false;
  }

  // _dd.document_version is a monotonic counter incremented with each new event
  // describing the same view: we'll need to increment it if we produce a new view event
  if (!ParseUInt64(extract(spans.dd_document_version), values.dd_document_version)) {
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

void RumViewEventParser::ScanSessionObject(JsonScanner& scanner) {
  // We expect 'session' to contain:
  // {"id":"5e551017-4114-4114-4114-beeeefbeeeef","type":"user"}
  if (scanner.EnterObject()) {
    while (scanner.OK() && scanner.Peek() != '}') {
      if (scanner.TrySkipObjectPropertyKey("type")) {
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

bool RumViewEventParser::ParseUUID(std::string_view json_literal, UUID& out_value) {
  // A UUID should be encoded as a JSON string exactly 36 bytes in length
  if (json_literal.size() != 38 || json_literal[0] != '"' || json_literal[37] != '"') {
    return false;
  }

  // Use UUID::Parse to decode the value enclosed by quotes, returning success
  auto uuid_opt = UUID::Parse(json_literal.substr(1, 36));
  if (!uuid_opt.has_value()) {
    return false;
  }
  out_value = *uuid_opt;
  return true;
}

bool RumViewEventParser::ParseString(
    std::string_view json_literal, std::string& out_value
) {
  // A string literal value should begin and end with double quotes; take `inner` as the
  // substring enclosed by those quotes
  if (json_literal.size() < 2 || json_literal.front() != '"' ||
      json_literal.back() != '"') {
    return false;
  }
  const std::string_view inner = json_literal.substr(1, json_literal.size() - 2);

  // In the typical case, our output string will need to hold `inner` exactly; if there
  // are any escape sequences, the final size will be slightly less than that
  out_value.clear();
  out_value.reserve(inner.size());

  // Begin iterating over `inner` using index `i`, which we'll advance
  // character-by-character, skipping over escape sequences as we decode them
  size_t i = 0;

  // Use a helper function to encapsulate the escape-sequence-parsing logic
  // clang-format off
  auto hex_nibble = [](char c) -> int {
    // Given [0..f] in hex, return an integer [0..15], or -1 if not valid hex
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
  };
  auto consume_escape_sequence = [&i, &inner, &hex_nibble](std::string& out) -> bool {
    // i is an offset into `inner`, currently positioned at a backslash, which
    // unambiguously begins an escape sequence within a JSON string literal: we need at
    // least one more byte after the slash, or this is a truncated escape
    if (i + 1 >= inner.size()) {
      return false;
    }

    // The escape sequence that follows the slash has at least one byte: advance to that
    // first byte and examine it
    ++i;
    switch(inner[i]) {
      // Single-character escape sequences are trivial: just add the escaped value to
      // the output string and increment `i` to move past it
      case '"': out += '"'; ++i; return true;
      case '\\': out += '\\'; ++i; return true;
      case 'b': out += '\b'; ++i; return true;
      case 'f': out += '\f'; ++i; return true;
      case 'n': out += '\n'; ++i; return true;
      case 'r': out += '\r'; ++i; return true;
      case 't': out += '\t'; ++i; return true;
      case '/': out += '/'; ++i; return true;
      // Hex-encoded '\uXXXX' escape sequences require parsing the encoded hex value,
      // but JSON produced by the SDK only writes '\u00XX' to handle unprintable ASCII
      // code points, since multi-byte UTF-8 data is preserved as-is
      case 'u': {
        // Verify that we have at least 4 bytes after '\u' and that the first two hex
        // digits are both 0
        if (i + 4 >= inner.size() || inner[i + 1] != '0' || inner[i + 2] != '0') {
          return false;
        }

        // Verify that the last two characters are both valid hex digits, get their
        // integer values, and combine them to form the 8-byte character value
        const int hi = hex_nibble(inner[i + 3]);
        const int lo = hex_nibble(inner[i + 4]);
        if (hi < 0 || lo < 0) {
          return false;
        }
        const char c = static_cast<char>((hi << 4) | lo);

        // Add that character to the output string, then advance past all 5 bytes of
        // 'uXXXX'
        out += c;
        i += 5;
        return true;
      }
      // Any other character appearing after '\' is an invalid escape sequence
      default: return false;
    }
  };
  // clang-format on

  // Iterate through the JSON value (sans enclosing quotes) and consume each character,
  // decoding and advancing past escape characters as we encounter them
  while (i < inner.size()) {
    const char c = inner[i];
    if (c == '\\') {
      if (!consume_escape_sequence(out_value)) {
        return false;
      }
    } else {
      out_value += c;
      ++i;
    }
  }
  return true;
}

bool RumViewEventParser::ParseUInt64(
    std::string_view json_literal, uint64_t& out_value
) {
  // Use <charconv>, checking that `ptr` ends up at the end of the literal to ensure
  // that we correctly reject non-integer values
  const char* begin = json_literal.data();
  const char* end = begin + json_literal.size();
  auto [ptr, ec] = std::from_chars(begin, end, out_value);
  return ec == std::errc{} && ptr == end;
}

bool RumViewEventParser::ParseBool(std::string_view json_literal, bool& out_value) {
  // Sanity-check: if no value, fail parsing
  if (json_literal.empty()) {
    return false;
  }

  // JsonScanner::SkipBoolLiteral already validates that the literal value associated
  // with this property is either `true` or `false`, so we can just peek at the first
  // character: if 't', we assume true; anything else is assumed false
  out_value = json_literal[0] == 't';
  return true;
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
