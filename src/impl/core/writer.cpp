#include "core/writer.hpp"

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
    DATADOG_ASSERT(num_bytes_written <= num_bytes, "uint wraparound");
    num_bytes_written += state.Write(write_ptr, num_bytes - num_bytes_written);

    // If we wrote the whole thing, exit the prefix state and prepare to write the first
    // event
    if (state.Done()) {
      state.Enter(Mode::Event, {});
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
    if (state.mode == Mode::Event) {
      // Populate state.s with a view of the next event block to write: state.Done() is
      // true when we haven't yet read a block, or when the block we last read has been
      // fully written
      while (state.Done() && !eof) {
        // Read the next TLV block from the file
        auto next = reader.ReadNext();
        if (!next.has_value()) {
          // Abort the request on any read failure
          return platform::HTTP_WRITE_RESULT_ABORT;
        }

        // Set EOF when we've read the last block
        eof = next->eof;

        // We only care about event blocks; if we hit a metadata block, skip it and keep
        // reading
        if (next->type != TLVBlockType::Event) {
          continue;
        }

        // We have an event block: accept it into our state struct, making state.Done()
        // no longer true
        state.Enter(Mode::Event, next->data);
      }

      // If we've read the entire file without finding any events, abort
      if (state.Done() && eof) {
        return platform::HTTP_WRITE_RESULT_ABORT;
      }

      // Write event data if we have it: state.Done() will be false here if we just read
      // a block and reset state.s, -or- if the previous callback invocation only had
      // partial space for the last block and we're resuming from where we left off
      if (!state.Done()) {
        // Write as much of the event data into the buffer as will fit
        DATADOG_ASSERT(num_bytes_written <= num_bytes, "uint wraparound");
        num_bytes_written += state.Write(write_ptr, num_bytes - num_bytes_written);

        // If we finished writing the whole event, exit the event state
        if (state.Done()) {
          if (eof) {
            // If we've read the last block, we just finished writing it
            state.Enter(Mode::Suffix, suffix);
          } else {
            // There are more events to read, so enter the delimiter state
            state.Enter(Mode::Delimiter, delimiter);
          }
        }

        // If we've filled the buffer, write no more
        DATADOG_ASSERT(num_bytes_written <= num_bytes, "buffer overrun");
        if (num_bytes_written >= num_bytes) {
          return num_bytes_written;
        }
      }
    } else {
      // Handle writing the delimiter that precedes the next event
      DATADOG_ASSERT(num_bytes_written <= num_bytes, "uint wraparound");
      num_bytes_written += state.Write(write_ptr, num_bytes - num_bytes_written);

      // Delimiter always precedes event
      if (state.Done()) {
        state.Enter(Mode::Event, {});
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
