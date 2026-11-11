// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profile_frame.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "datadog/impl/types/assert.hpp"
#include "datadog/impl/types/endian.hpp"

namespace datadog::impl {

// The only version that exists. A stored frame can be read by a later SDK than the one
// that wrote it, so a new layout can use a new version without breaking stored frames.
static constexpr uint8_t FRAME_VERSION = 1;

static bool fits_in_size_field(size_t size) {
  return size <= std::numeric_limits<uint32_t>::max();
}

/** Views a buffer of bytes as a Block. */
static Block as_block(const std::vector<uint8_t>& bytes) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/** The number of bytes that a part takes in a frame: its size, then the part itself. */
static size_t part_len(Block part) { return sizeof(uint32_t) + part.size(); }

/**
 * Writes a part's size, then the part itself, to `dst`, which has `n` bytes available:
 * `part_len(part)` of them are needed. Returns the number of bytes written.
 */
static size_t part_write(uint8_t* dst, size_t n, Block part) {
  const size_t num_bytes_written = part_len(part);
  DATADOG_ASSERT(
      num_bytes_written <= n, "buffer overflow on profile frame part encode"
  );

  WriteBigEndian32(dst, static_cast<uint32_t>(part.size()));
  std::copy(part.begin(), part.end(), dst + sizeof(uint32_t));
  return num_bytes_written;
}

/**
 * Reads the next part, a size followed by that many bytes, from the start of
 * `mut_remaining`, and removes it. Returns false, changing nothing, if there is no
 * complete part there.
 */
static bool read_part(std::string_view& mut_remaining, std::string_view& out_part) {
  if (mut_remaining.size() < sizeof(uint32_t)) {
    // Too short to hold a size; unable to read the part
    return false;
  }
  const size_t size = ReadBigEndian32(mut_remaining);
  const std::string_view after_size = mut_remaining.substr(sizeof(uint32_t));
  if (size > after_size.size()) {
    // The size runs past the end of the frame; unable to read the part
    return false;
  }
  out_part = after_size.substr(0, size);
  mut_remaining = after_size.substr(size);
  return true;
}

std::vector<uint8_t> EncodeProfileFrame(const ProfileMetadata& metadata, Block pprof) {
  const Block event_json = as_block(metadata.event_json);
  const Block rum_events_json = as_block(metadata.rum_events_json);

  if (!fits_in_size_field(event_json.size()) ||
      !fits_in_size_field(rum_events_json.size()) ||
      !fits_in_size_field(pprof.size())) {
    // A part's size doesn't fit in 32 bits; unable to encode a valid frame
    return {};
  }

  // Work out the size of the whole frame, so that it is allocated once, then write each
  // piece in turn
  const size_t frame_len =
      1 + part_len(event_json) + part_len(rum_events_json) + part_len(pprof);
  std::vector<uint8_t> frame(frame_len);

  uint8_t* ptr = frame.data();
  uint8_t* const dst_end = ptr + frame_len;

  *ptr++ = FRAME_VERSION;
  ptr += part_write(ptr, dst_end - ptr, event_json);
  ptr += part_write(ptr, dst_end - ptr, rum_events_json);
  ptr += part_write(ptr, dst_end - ptr, pprof);

  // The frame should be exactly full: the size computed above is exact
  DATADOG_ASSERT(ptr == dst_end, "profile frame size mismatch");
  return frame;
}

std::optional<ProfileFrameView> DecodeProfileFrame(std::string_view frame) {
  if (frame.empty() || static_cast<uint8_t>(frame.front()) != FRAME_VERSION) {
    // Empty or an unknown version; unable to parse
    return std::nullopt;
  }

  // Read the three parts in order, each of which must fit in what remains of the frame,
  // and then nothing may be left over
  std::string_view remaining = frame.substr(1);
  ProfileFrameView view;
  if (!read_part(remaining, view.event_json) ||
      !read_part(remaining, view.rum_events_json) ||
      !read_part(remaining, view.pprof) || !remaining.empty()) {
    // A part is missing or truncated, or data follows the last part; unable to parse
    return std::nullopt;
  }
  return view;
}

}  // namespace datadog::impl
