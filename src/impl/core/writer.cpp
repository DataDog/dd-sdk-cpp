#include "core/writer.hpp"

#include <cstring>

#include "assert.hpp"

namespace datadog::impl {

static size_t _chunked_write(
    char* dst, size_t dst_size, const char* src, size_t src_size
) {
  // Checks for zero size should occur before we're called
  DATADOG_ASSERT(dst_size > 0, "Attempted write with zero-length dst buffer");
  DATADOG_ASSERT(src_size > 0, "Attempted write with zero-length src buffer");

  // Copy from src as many bytes as will fit in dst
  const size_t num_bytes_to_copy = std::min(dst_size, src_size);
  std::memcpy(dst, src, num_bytes_to_copy);
  return num_bytes_to_copy;
}

static size_t _handle_chunked_write(
    char* dst, size_t dst_size, const char* src_buffer, size_t src_buffer_size,
    size_t& mut_offset
) {
  // Compute the position we're reading from and how many bytes are found there
  const char* src = src_buffer + mut_offset;
  const size_t src_size = src_buffer_size - mut_offset;

  // If there's nothing left to write, write 0 bytes and return
  if (src_size == 0) {
    return 0;
  }

  // Perform the write, then increment our stored offset
  const size_t num_bytes_written = _chunked_write(dst, dst_size, src, src_size);
  mut_offset += num_bytes_written;
  return num_bytes_written;
}

size_t StringWriter::operator()(char* buffer, size_t num_bytes) {
  // If an HTTP client implementation is silly enough to ask us to write to an empty
  // buffer, guard against it
  if (num_bytes == 0) {
    return 0;
  }

  return _handle_chunked_write(buffer, num_bytes, s.data(), s.size(), offset);
}

size_t TLVBatchWriter::State::Write(char*& mut_dst, size_t dst_size) {
  const size_t n = _handle_chunked_write(mut_dst, dst_size, s.data(), s.size(), offset);
  mut_dst += n;
  return n;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
size_t TLVBatchWriter::operator()(char* buffer, size_t num_bytes) {
  // Early-out if no write buffer, to make write-size calculations airtight
  if (num_bytes == 0) {
    return 0;
  }

  // We're using our BatchReader to read from a file, one TLV block at a time, into the
  // reader's reusable buffer: this is our "read buffer". The HTTP client provides us
  // with `buffer`, which we will populate with the next chunk of the request body up to
  // `num_bytes` in size: this is our "write buffer".
  char* write_ptr = buffer;      // Current write position
  size_t num_bytes_written = 0;  // Bytes written _in this callback invocation_

  // Write the prefix if we haven't already
  if (state.mode == Mode::Prefix) {
    // Write as much of the prefix as will fit
    num_bytes_written += state.Write(write_ptr, num_bytes - num_bytes_written);

    // If we wrote the whole thing, exit the prefix state and prepare to write the first
    // event, reading from the file until we find the first event block
    while (state.Done()) {
      // If we fail to read from the batch file, abort the request
      auto first = reader.ReadNext();
      if (!first.has_value()) {
        return platform::HTTP_WRITE_RESULT_ABORT;
      }

      // If we reach the end of the file without finding a single event block, abort the
      // request
      std::optional<TLVBlock> block = *first;
      if (!block) {
        return platform::HTTP_WRITE_RESULT_ABORT;
      }

      // If we found a metadata block, ignore it
      if (block->type != TLVBlockType::Event) {
        continue;
      }

      // We've found our first event: transition to writing it
      state.Enter(Mode::Event, block->data);
    }

    // If we've filled the buffer, write no more
    DATADOG_ASSERT(num_bytes_written <= num_bytes, "buffer overrun");
    if (num_bytes_written >= num_bytes) {
      return num_bytes_written;
    }
  }

  // Keep writing events, separated by the delimiter, until we run out of events or fill
  // the buffer
  while (state.mode == Mode::Event || state.mode == Mode::Delimiter) {
    // If we currently have an event block, begin (or resume) writing it
    if (state.mode == Mode::Event) {
      // Write as much of the event block as will fit
      num_bytes_written += state.Write(write_ptr, num_bytes - num_bytes_written);

      // If we've written the whole event, read from the file until we find another
      // event: if we find one, transition to writing the delimiter, followed by that
      // event; if we hit EOF, transition to writing the suffix
      while (state.Done()) {
        // Read the next TLV block from the file, aborting on any read failure
        auto next = reader.ReadNext();
        if (!next.has_value()) {
          return platform::HTTP_WRITE_RESULT_ABORT;
        }

        // Read OK: if we have no block, we're at EOF and we can wrap up
        std::optional<TLVBlock> block = *next;
        if (!block) {
          state.Enter(Mode::Suffix, suffix);
          break;
        }

        // Skip metadata blocks
        if (block->type != TLVBlockType::Event) {
          continue;
        }

        // We have an event block: keep track of it so we can transition to writing it
        // after the delimiter, but first write the delimiter
        next_event = block->data;
        state.Enter(Mode::Delimiter, delimiter);
        break;
      }

      // If we've filled the buffer, write no more
      DATADOG_ASSERT(num_bytes_written <= num_bytes, "buffer overrun");
      if (num_bytes_written >= num_bytes) {
        return num_bytes_written;
      }
    }

    // If we're currently writing a delimiter, continue writing it, then transition to
    // writing the subsequent event
    if (state.mode == Mode::Delimiter) {
      // Write as much of the delimiter as will fit
      num_bytes_written += state.Write(write_ptr, num_bytes - num_bytes_written);

      // If we've written the whole delimiter, transition to writing the next event
      // that we read previously
      if (state.Done()) {
        state.Enter(Mode::Event, next_event);
      }

      // If we've filled the buffer, write no more
      DATADOG_ASSERT(num_bytes_written <= num_bytes, "buffer overrun");
      if (num_bytes_written >= num_bytes) {
        return num_bytes_written;
      }
    }
  }

  // Write the suffix and we're done
  if (state.mode == Mode::Suffix) {
    // If we have more suffix data to write, write it
    if (!state.Done()) {
      DATADOG_ASSERT(num_bytes_written <= num_bytes, "uint wraparound");
      num_bytes_written += state.Write(write_ptr, num_bytes - num_bytes_written);
    }

    // Unconditionally return, as we can't write anything else: once we've finished
    // writing the suffix, the subsequent call to this function will fall down to this
    // point and return 0, signaling EOF to the HTTP client
    return num_bytes_written;
  }

  // Our state-machine logic shouldn't let us get here
  DATADOG_ASSERT(false, "Unexpected state in TLVBatchWriter");
  return platform::HTTP_WRITE_RESULT_ABORT;
}

}  // namespace datadog::impl
