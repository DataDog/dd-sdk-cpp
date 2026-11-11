// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profile_frame.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "support/catch.hpp"

using namespace datadog::impl;

static std::vector<uint8_t> to_bytes(std::string_view text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

static std::vector<uint8_t> to_bytes(std::initializer_list<int> values) {
  std::vector<uint8_t> bytes;
  for (const int value : values) {
    bytes.push_back(static_cast<uint8_t>(value));
  }
  return bytes;
}

/** Views a buffer as the string that the decoder takes. */
static std::string_view as_text(const std::vector<uint8_t>& buffer) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const char*>(buffer.data()), buffer.size()};
}

static std::string as_string(std::string_view view) { return std::string(view); }

static ProfileMetadata make_metadata(
    std::string_view event_json, std::string_view rum_json
) {
  ProfileMetadata metadata;
  metadata.event_json = to_bytes(event_json);
  metadata.rum_events_json = to_bytes(rum_json);
  return metadata;
}

// A valid frame whose parts are all non-empty. Its layout is spelled out in the tests
// below, which write frames by hand:
//
//   offset  0       version
//           1..4    size of event_json (2)
//           5..6    event_json
//           7..10   size of rum_events_json (2)
//           11..12  rum_events_json
//           13..16  size of pprof (3)
//           17..19  pprof
static std::vector<uint8_t> sample_frame() {
  return EncodeProfileFrame(make_metadata("{}", "[]"), as_text(to_bytes({1, 2, 3})));
}

TEST_CASE("EncodeProfileFrame layout", "[unit][profiling]") {
  const std::vector<uint8_t> frame = sample_frame();

  // The stored format must not change by accident: frames outlive the process that
  // wrote them. A version byte, then each part as a big-endian size and its body.
  const std::vector<uint8_t> expected = to_bytes({
      0x01,                    // version
      0x00, 0x00, 0x00, 0x02,  // event_json size
      '{',  '}',               // event_json
      0x00, 0x00, 0x00, 0x02,  // rum_events_json size
      '[',  ']',               // rum_events_json
      0x00, 0x00, 0x00, 0x03,  // pprof size
      0x01, 0x02, 0x03         // pprof
  });
  REQUIRE(frame == expected);
}

TEST_CASE("EncodeProfileFrame writes sizes big-endian", "[unit][profiling]") {
  // A part longer than 255 bytes shows which byte of the size is which. With the first
  // two parts empty, the pprof's size field is at offset 9.
  const std::vector<uint8_t> pprof(0x010203, 0xAB);
  const std::vector<uint8_t> frame =
      EncodeProfileFrame(make_metadata("", ""), as_text(pprof));

  REQUIRE(frame.size() == 13 + pprof.size());
  REQUIRE(frame[9] == 0x00);
  REQUIRE(frame[10] == 0x01);
  REQUIRE(frame[11] == 0x02);
  REQUIRE(frame[12] == 0x03);
}

TEST_CASE("DecodeProfileFrame round trip", "[unit][profiling]") {
  SECTION("text and binary parts") {
    // The pprof has every byte value, including zeros and bytes above 0x7F, which must
    // survive being viewed as chars
    std::vector<uint8_t> pprof;
    for (int i = 0; i < 256; i++) {
      pprof.push_back(static_cast<uint8_t>(i));
    }
    const ProfileMetadata metadata =
        make_metadata(R"({"family":"windows"})", R"([{"id":"1"},{"id":"2"}])");
    const std::vector<uint8_t> frame = EncodeProfileFrame(metadata, as_text(pprof));

    const auto decoded = DecodeProfileFrame(as_text(frame));
    REQUIRE(decoded.has_value());
    REQUIRE(as_string(decoded->event_json) == R"({"family":"windows"})");
    REQUIRE(as_string(decoded->rum_events_json) == R"([{"id":"1"},{"id":"2"}])");
    REQUIRE(decoded->pprof == as_text(pprof));
  }

  SECTION("views are into the frame") {
    const std::vector<uint8_t> frame = sample_frame();
    const std::string_view text = as_text(frame);

    const auto decoded = DecodeProfileFrame(text);
    REQUIRE(decoded.has_value());
    // Each part's body is at its place in the frame, with no copies made
    REQUIRE(decoded->event_json.data() == text.data() + 5);
    REQUIRE(decoded->rum_events_json.data() == text.data() + 11);
    REQUIRE(decoded->pprof.data() == text.data() + 17);
    REQUIRE(decoded->pprof.data() + decoded->pprof.size() == text.data() + text.size());
  }

  SECTION("a large profile") {
    const std::vector<uint8_t> pprof(3 * 1024 * 1024, 0x5A);
    const std::vector<uint8_t> frame =
        EncodeProfileFrame(make_metadata("{}", "[]"), as_text(pprof));

    const auto decoded = DecodeProfileFrame(as_text(frame));
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->pprof.size() == pprof.size());
    REQUIRE(decoded->pprof == as_text(pprof));
  }
}

TEST_CASE("DecodeProfileFrame with empty parts", "[unit][profiling]") {
  SECTION("all parts empty") {
    const std::vector<uint8_t> frame = EncodeProfileFrame(make_metadata("", ""), {});
    // Only the version byte and three zero sizes
    REQUIRE(frame.size() == 13);

    const auto decoded = DecodeProfileFrame(as_text(frame));
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->event_json.empty());
    REQUIRE(decoded->rum_events_json.empty());
    REQUIRE(decoded->pprof.empty());
  }

  SECTION("one part empty") {
    {
      const auto frame =
          EncodeProfileFrame(make_metadata("", "[]"), as_text(to_bytes({7})));
      const auto decoded = DecodeProfileFrame(as_text(frame));
      REQUIRE(decoded.has_value());
      REQUIRE(decoded->event_json.empty());
      REQUIRE(as_string(decoded->rum_events_json) == "[]");
      REQUIRE(decoded->pprof == as_text(to_bytes({7})));
    }
    {
      const auto frame =
          EncodeProfileFrame(make_metadata("{}", ""), as_text(to_bytes({7})));
      const auto decoded = DecodeProfileFrame(as_text(frame));
      REQUIRE(decoded.has_value());
      REQUIRE(as_string(decoded->event_json) == "{}");
      REQUIRE(decoded->rum_events_json.empty());
      REQUIRE(decoded->pprof == as_text(to_bytes({7})));
    }
    {
      const auto frame = EncodeProfileFrame(make_metadata("{}", "[]"), {});
      const auto decoded = DecodeProfileFrame(as_text(frame));
      REQUIRE(decoded.has_value());
      REQUIRE(as_string(decoded->event_json) == "{}");
      REQUIRE(as_string(decoded->rum_events_json) == "[]");
      REQUIRE(decoded->pprof.empty());
    }
  }
}

TEST_CASE("DecodeProfileFrame rejects malformed frames", "[unit][profiling]") {
  const std::vector<uint8_t> frame = sample_frame();
  REQUIRE(DecodeProfileFrame(as_text(frame)).has_value());

  SECTION("empty") { REQUIRE_FALSE(DecodeProfileFrame({}).has_value()); }

  SECTION("truncated at every size") {
    // Anywhere short of the whole frame: inside a size field, or inside a body
    for (size_t size = 0; size < frame.size(); size++) {
      INFO("size " << size);
      REQUIRE_FALSE(DecodeProfileFrame(as_text(frame).substr(0, size)).has_value());
    }
  }

  SECTION("data after the last part") {
    std::vector<uint8_t> extended = frame;
    extended.push_back(0x00);
    REQUIRE_FALSE(DecodeProfileFrame(as_text(extended)).has_value());
  }

  SECTION("unknown version") {
    for (const int version : {0, 2, 0x80, 0xFF}) {
      INFO("version " << version);
      std::vector<uint8_t> bad = frame;
      bad[0] = static_cast<uint8_t>(version);
      REQUIRE_FALSE(DecodeProfileFrame(as_text(bad)).has_value());
    }
  }

  SECTION("a missing part") {
    // The version and only the first two parts, which are complete in themselves
    const std::vector<uint8_t> two_parts = to_bytes({
        0x01,  // version
        0x00,
        0x00,
        0x00,
        0x02,  // event_json size
        '{',
        '}',  // event_json
        0x00,
        0x00,
        0x00,
        0x02,  // rum_events_json size
        '[',
        ']'  // rum_events_json
    });
    REQUIRE_FALSE(DecodeProfileFrame(as_text(two_parts)).has_value());
  }

  SECTION("a size that is too small") {
    // The pprof's size is 2, but 3 bytes follow it: one is left over
    std::vector<uint8_t> bad = frame;
    bad[16] = 0x02;
    REQUIRE_FALSE(DecodeProfileFrame(as_text(bad)).has_value());
  }

  SECTION("a size that is too large") {
    // The pprof's size is 4, but only 3 bytes follow it
    std::vector<uint8_t> bad = frame;
    bad[16] = 0x04;
    REQUIRE_FALSE(DecodeProfileFrame(as_text(bad)).has_value());
  }

  SECTION("a size that is far too large") {
    // 0xFFFFFFFF in each part's size field in turn: the offsets of the three fields
    for (const size_t size_offset : {1, 7, 13}) {
      INFO("size field at " << size_offset);
      std::vector<uint8_t> bad = frame;
      for (size_t i = 0; i < 4; i++) {
        bad[size_offset + i] = 0xFF;
      }
      REQUIRE_FALSE(DecodeProfileFrame(as_text(bad)).has_value());
    }
  }

  SECTION("a part that takes the bytes of the next part's size") {
    // The first part claims 6 bytes, and 6 do follow, but the last 4 look like the
    // second part's size field. Nothing is left for the second and third parts.
    const std::vector<uint8_t> bad = to_bytes({
        0x01,  // version
        0x00,
        0x00,
        0x00,
        0x06,  // event_json size
        '{',
        '}',  // the first 2 bytes of the claimed 6
        0x00,
        0x00,
        0x00,
        0x00  // the other 4
    });
    REQUIRE_FALSE(DecodeProfileFrame(as_text(bad)).has_value());
  }
}
