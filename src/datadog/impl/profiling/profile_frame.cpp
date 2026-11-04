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

static bool _fits_in_size_field(size_t size) {
  return size <= std::numeric_limits<uint32_t>::max();
}

/** The number of bytes that a part takes in a frame: its size, then the part itself. */
static size_t _part_len(const std::vector<uint8_t>& part) {
  return sizeof(uint32_t) + part.size();
}

/**
 * Writes a part's size, then the part itself, to `dst`, which has `n` bytes available:
 * `_part_len(part)` of them are needed. Returns the number of bytes written.
 */
static size_t _part_write(uint8_t* dst, size_t n, const std::vector<uint8_t>& part) {
  const size_t num_bytes_written = _part_len(part);
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
static bool _read_part(std::string_view& mut_remaining, std::string_view& out_part) {
  if (mut_remaining.size() < sizeof(uint32_t)) {
    return false;
  }
  const size_t size = ReadBigEndian32(mut_remaining.data());
  const std::string_view after_size = mut_remaining.substr(sizeof(uint32_t));
  if (size > after_size.size()) {
    return false;
  }
  out_part = after_size.substr(0, size);
  mut_remaining = after_size.substr(size);
  return true;
}

std::vector<uint8_t> EncodeProfileFrame(
    const ProfileMetadata& metadata, const std::vector<uint8_t>& pprof
) {
  const std::vector<uint8_t>& event_json = metadata.event_json;
  const std::vector<uint8_t>& rum_events_json = metadata.rum_events_json;

  if (!_fits_in_size_field(event_json.size()) ||
      !_fits_in_size_field(rum_events_json.size()) ||
      !_fits_in_size_field(pprof.size())) {
    return {};
  }

  // Work out the size of the whole frame, so that it is allocated once, then write each
  // piece in turn
  const size_t frame_len =
      1 + _part_len(event_json) + _part_len(rum_events_json) + _part_len(pprof);
  std::vector<uint8_t> frame(frame_len);

  uint8_t* ptr = frame.data();
  uint8_t* const dst_end = ptr + frame_len;

  *ptr++ = FRAME_VERSION;
  ptr += _part_write(ptr, dst_end - ptr, event_json);
  ptr += _part_write(ptr, dst_end - ptr, rum_events_json);
  ptr += _part_write(ptr, dst_end - ptr, pprof);

  // The frame should be exactly full: the size computed above is exact
  DATADOG_ASSERT(ptr == dst_end, "profile frame size mismatch");
  return frame;
}

std::optional<ProfileFrameView> DecodeProfileFrame(std::string_view frame) {
  if (frame.empty() || static_cast<uint8_t>(frame.front()) != FRAME_VERSION) {
    return std::nullopt;
  }

  // Read the three parts in order, each of which must fit in what remains of the frame,
  // and then nothing may be left over
  std::string_view remaining = frame.substr(1);
  ProfileFrameView view;
  if (!_read_part(remaining, view.event_json) ||
      !_read_part(remaining, view.rum_events_json) ||
      !_read_part(remaining, view.pprof) || !remaining.empty()) {
    return std::nullopt;
  }
  return view;
}

}  // namespace datadog::impl
