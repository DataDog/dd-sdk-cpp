// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/profiling/profile_metadata.hpp"

namespace datadog::impl {

/**
 * The parts of a profile, as they are stored: views into the frame that was decoded, so
 * valid only as long as that frame's memory is.
 */
struct ProfileFrameView {
  /** The content of the `event.json` part of the upload. */
  std::string_view event_json;
  /** The content of the `rum-mobile-events.json` part of the upload. */
  std::string_view rum_events_json;
  /** The content of the `profile.pprof` part of the upload. */
  std::string_view pprof;
};

/**
 * Encodes everything needed to upload one profile into a single block of bytes, which
 * is stored as one event.
 *
 * A profile is uploaded as three parts, and the upload streams them out of the block
 * read from a batch file. That block is the only thing the reader holds on to, so all
 * three parts have to be in it together.
 *
 * === Frame format ===
 *
 * A frame is a version byte, followed by three parts, each written as its size and then
 * its body, with nothing before, between or after them. Like the Core's TLV blocks,
 * each part says how long it is, so a reader can walk the frame one part at a time:
 *
 *     version      1 byte: always 1
 *     size         4 bytes: the size of `event_json`, in bytes
 *     event_json   `size` bytes
 *     size         4 bytes: the size of `rum_events_json`, in bytes
 *     rum_events   `size` bytes
 *     size         4 bytes: the size of `pprof`, in bytes
 *     pprof        `size` bytes
 *
 * The parts always come in that order, and all three are always present. Sizes are
 * unsigned 32-bit integers, big-endian, like the other multi-byte binary values that
 * the Core writes. A part may be empty, so the smallest frame is 13 bytes: the version
 * and three zero sizes.
 *
 * For example, a frame whose `event_json` is `{}`, whose `rum_events_json` is `[]`, and
 * whose `pprof` is the three bytes 01 02 03, is 20 bytes long:
 *
 *     offset  bytes                  meaning
 *     0       01                     version
 *     1..4    00 00 00 02            size of `event_json`: 2
 *     5..6    7B 7D                  `event_json`: `{}`
 *     7..10   00 00 00 02            size of `rum_events_json`: 2
 *     11..12  5B 5D                  `rum_events_json`: `[]`
 *     13..16  00 00 00 03            size of `pprof`: 3
 *     17..19  01 02 03               `pprof`
 *
 * A stored frame can outlive the SDK that wrote it, so the format must not change in a
 * way that an older reader would misread: a new layout needs a new version.
 *
 * Returns an empty vector, which is never a valid frame, if a part is too large for its
 * size to be stored.
 */
std::vector<uint8_t> EncodeProfileFrame(const ProfileMetadata& metadata, Block pprof);

/**
 * Decodes a frame written by EncodeProfileFrame(), returning views of its parts, or
 * null if `frame` is not exactly one valid frame.
 */
std::optional<ProfileFrameView> DecodeProfileFrame(std::string_view frame);

}  // namespace datadog::impl
