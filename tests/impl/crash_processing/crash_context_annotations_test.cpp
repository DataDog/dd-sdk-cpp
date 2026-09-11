// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_processing/crash_context_annotations.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/attribute.hpp"
#include "datadog/core.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/types/crash_annotations.hpp"
#include "datadog/impl/types/crash_reporting.hpp"
#include "datadog/impl/types/json.hpp"

#include "support/catch.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

/**
 * Stand-in for a set of crashpad::StringAnnotation values that have been stored in a
 * std::map<std::string, std::string>.
 */
struct AnnotationSet {
  std::map<std::string, std::string> values;

  /**
   * JSON-encodes a value of type T and stores that as the value of an annotation with
   * the name given by `key`.
   */
  template <typename T>
  void Encode(std::string_view key, const T& value) {
    std::vector<uint8_t> buf;
    EncodeJson(buf, value);
    values[std::string(key)] =
        std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
  }

  /**
   * Given a CrashContext value, populates the entire `values` map with the set of data
   * that would be written to annotations by the Crashpad-based ICrashHandler.
   */
  void Populate(const CrashContext& ctx) {
    if (ctx.tracking_consent == TrackingConsent::Granted) {
      values["dd.tracking_consent"] = "granted";
    } else if (ctx.tracking_consent == TrackingConsent::NotGranted) {
      values["dd.tracking_consent"] = "not-granted";
    } else {
      values["dd.tracking_consent"] = "pending";
    }
    Encode(
        "dd.config",
        ConfigAnnotation{
            ctx.service,
            ctx.env,
            ctx.application_version,
            ctx.variant,
            ctx.source,
            ctx.sdk_version,
        }
    );
    Encode(
        "dd.os",
        OsAnnotation{
            ctx.os_name,
            ctx.os_version,
            ctx.os_build,
            ctx.os_version_major,
        }
    );
    Encode(
        "dd.device",
        DeviceAnnotation{
            ctx.device_type,
            ctx.device_name,
            ctx.device_model,
            ctx.device_brand,
            ctx.device_architecture,
            ctx.device_locale,
            ctx.device_time_zone,
        }
    );
    Encode(
        "dd.usr",
        UsrAnnotation{
            ctx.user_id,
            ctx.user_name,
            ctx.user_email,
            ctx.user_anonymous_id,
            ctx.user_extra,
        }
    );
    Encode(
        "dd.account",
        AccountAnnotation{
            ctx.account_id,
            ctx.account_name,
            ctx.account_extra,
        }
    );
    Encode(
        "dd.rum.config",
        RumConfigAnnotation{
            ctx.rum_initial_config.application_id,
            ctx.rum_initial_config.session_sample_rate,
        }
    );
    Encode(
        "dd.rum.session",
        RumSessionAnnotation{
            ctx.rum_session_state.session_id,
            ctx.rum_session_state.is_sampled,
            ctx.rum_session_state.is_active,
            ctx.rum_session_state.is_initial_session,
            ctx.rum_session_state.has_tracked_any_view,
            ctx.rum_session_state.did_start_with_replay,
        }
    );
    Encode("dd.rum.attributes", ctx.global_rum_attributes);
    values["dd.rum.last_view"] = ctx.last_view_event_json;
  }
};

TEST_CASE("ParseCrashContextFromAnnotations", "[unit][crash_processing]") {
  SECTION("M produce CrashContext that serializes to identical JSON annotations") {
    // Given a fully-populated CrashContext value
    CrashContext ctx{};
    {
      ctx.service = "my-service";
      ctx.env = "prod";
      ctx.application_version = "2.3.4";
      ctx.variant = "release";
      ctx.source = "cpp";
      ctx.sdk_version = "3.0.0";
      ctx.tracking_consent = TrackingConsent::Granted;

      ctx.os_name = "CoolOS";
      ctx.os_version = "10.0";
      ctx.os_build = "abc123";
      ctx.os_version_major = "10";

      ctx.device_type = "desktop";
      ctx.device_name = "My Machine";
      ctx.device_model = "Model X";
      ctx.device_brand = "Acme";
      ctx.device_architecture = "x86_64";
      ctx.device_locale = "en-US";
      ctx.device_time_zone = "America/New_York";

      ctx.user_id = "u-abc123";
      ctx.user_name = "Test User";
      ctx.user_email = "test@example.com";
      ctx.user_anonymous_id = *UUID::Parse("b0b0cafe-1234-5678-abcd-b0b0cafeb0b0");
      Attribute user_extra = Attribute::Object(1);
      user_extra.SetObjectProperty("middle_initial", Attribute::String("Q"));
      ctx.user_extra = user_extra;

      ctx.account_id = "acct-42";
      ctx.account_name = "Test Account";
      Attribute account_extra = Attribute::Object(1);
      account_extra.SetObjectProperty("tier", Attribute::String("gold"));
      ctx.account_extra = account_extra;

      ctx.rum_initial_config.application_id =
          *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
      ctx.rum_initial_config.session_sample_rate = 75.0f;
      ctx.rum_session_state.session_id =
          *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
      ctx.rum_session_state.is_sampled = true;
      ctx.rum_session_state.is_active = true;
      ctx.rum_session_state.is_initial_session = false;
      ctx.rum_session_state.has_tracked_any_view = true;
      ctx.rum_session_state.did_start_with_replay = false;

      ctx.last_view_event_json =
          R"({"type":"view","application":{"id":"a991ca10-4004-4004-4004-beefbeefbeef"}})";

      Attribute global_attrs = Attribute::Object(3);
      global_attrs.SetObjectProperty("flags", Attribute::UInt(0xf0e1f));
      global_attrs.SetObjectProperty("enabled", Attribute::Bool(true));
      global_attrs.SetObjectProperty("ratio", Attribute::Double(0.5));
      ctx.global_rum_attributes = global_attrs;
    }

    // And the set of annotation values that would be received by the Crashpad handler
    // process as a std::map<std::string, std::string>
    AnnotationSet ann;
    ann.Populate(ctx);

    // When we parse a new CrashContext struct from those encoded annotation strings
    const CrashContext got = ParseCrashContextFromAnnotations(ann.values);

    // Then round-tripping that value back to encoded annotations gives us exactly the
    // same set of JSON payloads
    AnnotationSet reencoded;
    reencoded.Populate(got);
    REQUIRE(ann.values == reencoded.values);
  }

  SECTION("M produce default-initialized CrashContext W parsed from empty map") {
    // Given an empty set of annotation values
    std::map<std::string, std::string> values;

    // When we parse that empty set to a CrashContext struct
    const CrashContext got = ParseCrashContextFromAnnotations(values);

    // Then we end up with a value that contains empty/default-initialized members,
    // which will simply be dropped
    REQUIRE(got.tracking_consent == TrackingConsent::Pending);
    REQUIRE(got.os_name == "");
    REQUIRE(got.rum_initial_config.application_id == UUID::Zero);
  }
}
