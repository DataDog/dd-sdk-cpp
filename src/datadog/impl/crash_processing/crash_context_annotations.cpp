// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_processing/crash_context_annotations.hpp"

#include <string_view>

#include "datadog/attribute.hpp"

#include "datadog/impl/types/json/json_scanner.hpp"
#include "datadog/impl/types/json/parse_attribute.hpp"
#include "datadog/impl/types/json/parse_primitives.hpp"

namespace datadog::impl {

namespace {

/**
 * Looks up `key` in `params`. Returns an empty string_view if absent.
 */
std::string_view FindAnnotation(
    const std::map<std::string, std::string>& params, std::string_view key
) {
  auto it = params.find(std::string(key));
  if (it == params.end()) {
    return {};
  }
  return it->second;
}

/**
 * Tries to advance `sc` past a JSON string literal value, writing the unescaped result
 * into `out`. Returns true and updates `out` on success; returns false (leaving `sc`
 * failed) if the scanner is not positioned at a string literal. ParseJsonString failure
 * is treated as best-effort: if the literal is structurally valid (SkipStringLiteral
 * succeeded) but contains an unrecognised escape sequence, `out` is left unchanged and
 * the scanner continues.
 */
bool TryReadStringProperty(JsonScanner& sc, std::string_view json, std::string& out) {
  auto span = sc.SkipStringLiteral();
  if (!span.OK()) {
    return false;
  }
  ParseJsonString(json.substr(span.i, span.len), out);
  return true;
}

/**
 * Reads the next property from an object being scanned, collecting its value as an
 * Attribute and inserting it into `extra`. Expects `sc` to be positioned at the start
 * of a property key within an already-entered object. If the key or value is malformed,
 * fails `sc` and returns.
 */
void CollectExtraProperty(JsonScanner& sc, std::string_view json, Attribute& extra) {
  auto key_span = sc.SkipStringLiteral();
  if (!key_span.OK()) {
    return;
  }
  std::string key_str;
  if (!ParseJsonString(json.substr(key_span.i, key_span.len), key_str)) {
    sc.Fail();
    return;
  }
  if (sc.Peek() != ':') {
    sc.Fail();
    return;
  }
  sc.Advance();
  auto val_span = sc.SkipValue();
  if (!val_span.OK()) {
    return;
  }
  Attribute val;
  if (ParseJsonAttribute(json.substr(val_span.i, val_span.len), val)) {
    extra.SetObjectProperty(key_str, val);
  }
}

/**
 * Parses dd.config into the relevant CrashContext string fields.
 */
void ParseConfigAnnotation(std::string_view json, CrashContext& ctx) {
  JsonScanner sc{json};
  if (!sc.EnterObject()) {
    return;
  }
  while (sc.OK() && sc.Peek() != '}') {
    if (sc.TrySkipObjectPropertyKey("service")) {
      TryReadStringProperty(sc, json, ctx.service);
    } else if (sc.TrySkipObjectPropertyKey("env")) {
      TryReadStringProperty(sc, json, ctx.env);
    } else if (sc.TrySkipObjectPropertyKey("version")) {
      TryReadStringProperty(sc, json, ctx.application_version);
    } else if (sc.TrySkipObjectPropertyKey("variant")) {
      TryReadStringProperty(sc, json, ctx.variant);
    } else if (sc.TrySkipObjectPropertyKey("source")) {
      TryReadStringProperty(sc, json, ctx.source);
    } else if (sc.TrySkipObjectPropertyKey("sdk_version")) {
      TryReadStringProperty(sc, json, ctx.sdk_version);
    } else {
      sc.SkipObjectProperty();
    }
    sc.SkipObjectPropertySeparator();
  }
}

/**
 * Parses dd.os into the relevant CrashContext string fields.
 */
void ParseOsAnnotation(std::string_view json, CrashContext& ctx) {
  JsonScanner sc{json};
  if (!sc.EnterObject()) {
    return;
  }
  while (sc.OK() && sc.Peek() != '}') {
    if (sc.TrySkipObjectPropertyKey("name")) {
      TryReadStringProperty(sc, json, ctx.os_name);
    } else if (sc.TrySkipObjectPropertyKey("version")) {
      TryReadStringProperty(sc, json, ctx.os_version);
    } else if (sc.TrySkipObjectPropertyKey("build")) {
      TryReadStringProperty(sc, json, ctx.os_build);
    } else if (sc.TrySkipObjectPropertyKey("version_major")) {
      TryReadStringProperty(sc, json, ctx.os_version_major);
    } else {
      sc.SkipObjectProperty();
    }
    sc.SkipObjectPropertySeparator();
  }
}

/**
 * Parses dd.device into the relevant CrashContext string fields.
 */
void ParseDeviceAnnotation(std::string_view json, CrashContext& ctx) {
  JsonScanner sc{json};
  if (!sc.EnterObject()) {
    return;
  }
  while (sc.OK() && sc.Peek() != '}') {
    if (sc.TrySkipObjectPropertyKey("type")) {
      TryReadStringProperty(sc, json, ctx.device_type);
    } else if (sc.TrySkipObjectPropertyKey("name")) {
      TryReadStringProperty(sc, json, ctx.device_name);
    } else if (sc.TrySkipObjectPropertyKey("model")) {
      TryReadStringProperty(sc, json, ctx.device_model);
    } else if (sc.TrySkipObjectPropertyKey("brand")) {
      TryReadStringProperty(sc, json, ctx.device_brand);
    } else if (sc.TrySkipObjectPropertyKey("architecture")) {
      TryReadStringProperty(sc, json, ctx.device_architecture);
    } else if (sc.TrySkipObjectPropertyKey("locale")) {
      TryReadStringProperty(sc, json, ctx.device_locale);
    } else if (sc.TrySkipObjectPropertyKey("time_zone")) {
      TryReadStringProperty(sc, json, ctx.device_time_zone);
    } else {
      sc.SkipObjectProperty();
    }
    sc.SkipObjectPropertySeparator();
  }
}

/**
 * Parses dd.usr into the relevant CrashContext user fields.
 */
void ParseUsrAnnotation(std::string_view json, CrashContext& ctx) {
  JsonScanner sc{json};
  if (!sc.EnterObject()) {
    return;
  }
  Attribute extra = Attribute::Object();
  while (sc.OK() && sc.Peek() != '}') {
    if (sc.TrySkipObjectPropertyKey("id")) {
      TryReadStringProperty(sc, json, ctx.user_id);
    } else if (sc.TrySkipObjectPropertyKey("name")) {
      TryReadStringProperty(sc, json, ctx.user_name);
    } else if (sc.TrySkipObjectPropertyKey("email")) {
      TryReadStringProperty(sc, json, ctx.user_email);
    } else if (sc.TrySkipObjectPropertyKey("anonymous_id")) {
      auto span = sc.SkipStringLiteral();
      if (span.OK()) {
        ParseJsonUUID(json.substr(span.i, span.len), ctx.user_anonymous_id);
      }
    } else {
      CollectExtraProperty(sc, json, extra);
    }
    sc.SkipObjectPropertySeparator();
  }
  ctx.user_extra = extra;
}

/**
 * Parses dd.account into the relevant CrashContext account fields.
 */
void ParseAccountAnnotation(std::string_view json, CrashContext& ctx) {
  JsonScanner sc{json};
  if (!sc.EnterObject()) {
    return;
  }
  Attribute extra = Attribute::Object();
  while (sc.OK() && sc.Peek() != '}') {
    if (sc.TrySkipObjectPropertyKey("id")) {
      TryReadStringProperty(sc, json, ctx.account_id);
    } else if (sc.TrySkipObjectPropertyKey("name")) {
      TryReadStringProperty(sc, json, ctx.account_name);
    } else {
      CollectExtraProperty(sc, json, extra);
    }
    sc.SkipObjectPropertySeparator();
  }
  ctx.account_extra = extra;
}

/**
 * Parses dd.rum.config into the relevant CrashContext rum_initial_config fields.
 */
void ParseRumConfigAnnotation(std::string_view json, CrashContext& ctx) {
  JsonScanner sc{json};
  if (!sc.EnterObject()) {
    return;
  }
  while (sc.OK() && sc.Peek() != '}') {
    if (sc.TrySkipObjectPropertyKey("application_id")) {
      auto span = sc.SkipStringLiteral();
      if (span.OK()) {
        ParseJsonUUID(
            json.substr(span.i, span.len), ctx.rum_initial_config.application_id
        );
      }
    } else if (sc.TrySkipObjectPropertyKey("session_sample_rate")) {
      auto span = sc.SkipNumberLiteral();
      if (span.OK()) {
        double d = 0.0;
        if (ParseJsonDouble(json.substr(span.i, span.len), d)) {
          ctx.rum_initial_config.session_sample_rate = static_cast<float>(d);
        }
      }
    } else {
      sc.SkipObjectProperty();
    }
    sc.SkipObjectPropertySeparator();
  }
}

/**
 * Parses dd.rum.session into the relevant CrashContext rum_session_state fields.
 */
void ParseRumSessionAnnotation(std::string_view json, CrashContext& ctx) {
  JsonScanner sc{json};
  if (!sc.EnterObject()) {
    return;
  }
  while (sc.OK() && sc.Peek() != '}') {
    if (sc.TrySkipObjectPropertyKey("id")) {
      auto span = sc.SkipStringLiteral();
      if (span.OK()) {
        ParseJsonUUID(json.substr(span.i, span.len), ctx.rum_session_state.session_id);
      }
    } else if (sc.TrySkipObjectPropertyKey("is_sampled")) {
      auto span = sc.SkipBoolLiteral();
      if (span.OK()) {
        ParseJsonBool(json.substr(span.i, span.len), ctx.rum_session_state.is_sampled);
      }
    } else if (sc.TrySkipObjectPropertyKey("is_active")) {
      auto span = sc.SkipBoolLiteral();
      if (span.OK()) {
        ParseJsonBool(json.substr(span.i, span.len), ctx.rum_session_state.is_active);
      }
    } else if (sc.TrySkipObjectPropertyKey("is_initial")) {
      auto span = sc.SkipBoolLiteral();
      if (span.OK()) {
        ParseJsonBool(
            json.substr(span.i, span.len), ctx.rum_session_state.is_initial_session
        );
      }
    } else if (sc.TrySkipObjectPropertyKey("has_tracked_any_view")) {
      auto span = sc.SkipBoolLiteral();
      if (span.OK()) {
        ParseJsonBool(
            json.substr(span.i, span.len), ctx.rum_session_state.has_tracked_any_view
        );
      }
    } else if (sc.TrySkipObjectPropertyKey("did_start_with_replay")) {
      auto span = sc.SkipBoolLiteral();
      if (span.OK()) {
        ParseJsonBool(
            json.substr(span.i, span.len), ctx.rum_session_state.did_start_with_replay
        );
      }
    } else {
      sc.SkipObjectProperty();
    }
    sc.SkipObjectPropertySeparator();
  }
}

}  // namespace

CrashContext ParseCrashContextFromAnnotations(
    const std::map<std::string, std::string>& params
) {
  CrashContext ctx{};

  // dd.tracking_consent: plain string, not JSON
  auto consent_str = FindAnnotation(params, "dd.tracking_consent");
  if (consent_str == "granted") {
    ctx.tracking_consent = TrackingConsent::Granted;
  } else if (consent_str == "not-granted") {
    ctx.tracking_consent = TrackingConsent::NotGranted;
  } else {
    ctx.tracking_consent = TrackingConsent::Pending;
  }

  // dd.config
  if (auto json = FindAnnotation(params, "dd.config"); !json.empty()) {
    ParseConfigAnnotation(json, ctx);
  }

  // dd.os
  if (auto json = FindAnnotation(params, "dd.os"); !json.empty()) {
    ParseOsAnnotation(json, ctx);
  }

  // dd.device
  if (auto json = FindAnnotation(params, "dd.device"); !json.empty()) {
    ParseDeviceAnnotation(json, ctx);
  }

  // dd.usr
  if (auto json = FindAnnotation(params, "dd.usr"); !json.empty()) {
    ParseUsrAnnotation(json, ctx);
  }

  // dd.account
  if (auto json = FindAnnotation(params, "dd.account"); !json.empty()) {
    ParseAccountAnnotation(json, ctx);
  }

  // dd.rum.config
  if (auto json = FindAnnotation(params, "dd.rum.config"); !json.empty()) {
    ParseRumConfigAnnotation(json, ctx);
  }

  // dd.rum.session
  if (auto json = FindAnnotation(params, "dd.rum.session"); !json.empty()) {
    ParseRumSessionAnnotation(json, ctx);
  }

  // dd.rum.attributes: parse entire top-level object as Attribute::Object
  if (auto json = FindAnnotation(params, "dd.rum.attributes"); !json.empty()) {
    Attribute attrs;
    if (ParseJsonAttribute(json, attrs)) {
      ctx.global_rum_attributes = attrs;
    }
  }

  // dd.rum.last_view: copy raw string, normalizing "{}" and "null" to ""
  if (auto json = FindAnnotation(params, "dd.rum.last_view"); !json.empty()) {
    if (json != "{}" && json != "null") {
      ctx.last_view_event_json = std::string(json);
    }
  }

  return ctx;
}

}  // namespace datadog::impl
