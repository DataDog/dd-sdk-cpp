// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/attribute.hpp"

#include "datadog/impl/core/storage/filesystem.hpp"

namespace datadog::impl {

class File;

/**
 * Low-level binary serialization routines for attribute values, used when the SDK needs
 * to persist user-specified attribute values to disk in order to read them back in a
 * future process.
 *
 * In uploaded events, Attribute values are typically serialized as JSON using the code
 * in `impl/core/json/attribute.hpp`.
 *
 * However, `CrashReporting` needs to flush attribute values to disk as quickly and
 * simply as possible, and in a form that the SDK can easily deserialize. This simple
 * binary format allows us to dump and parse attributes without full JSON-parsing
 * support.
 *
 * === File format ===
 *
 * Any Attribute is serialized as a uint8_t type identifier (corresponding to
 * datadog::ValueType), followed by a value payload.
 *
 * - ValueType::Null has a zero-size payload.
 *
 * - Primitive types (Bool, Int, UInt, Timestamp, Double) have a fixed-size payload of 8
 *   bytes, using the appropriate storage type:
 *
 *     - Bool: 0x0 or 0x1
 *     - Int, Timestamp: int64_t
 *     - UInt: uint64_t
 *     - Double: double
 *
 * - ValueType::UUID has a fixed-size payload of 16 bytes, containing the raw bytes of
 *   the UUID value.
 *
 * - ValueType::String has a payload consisting of an initial 8-byte size_t indicating
 *   the length of the string in bytes, followed by `length` bytes of raw string data,
 *   with no null terminator.
 *
 * - ValueType::Array has a payload consisting of an initial 8-byte size_t indicating
 *   the number of items in the array, then a sequence of that many binary-encoded
 *   Attributes thereafter.
 *
 * - ValueType::Object has a payload consisting of an initial 8-byte size_t indicating
 *   the number of properties contained in the object, then a sequence of that many
 *   entries, where each entry consists of a.) a length-prefixed string indicating the
 *   property name, encoded as in the case of ValueType::String, immediately followed by
 *   b.) the binary-encoded Attribute for that property value.
 *
 * Endianness: All values are encoded in native byte order.
 *
 * This format is intended strictly for internal use. Versioning and compatibility are
 * the responsibility of the enclosing format, e.g. `CrashContext`. No effort is made to
 * encode file format versions etc. into individual `Attribute` values.
 *
 * === Example ===
 *
 * Given an Attribute value encoding {"foo":100,"bar":[null,true,"hello"]}, the
 * equivalent binary representation would be:
 *
 * 0x0000: <ValueType::Object>  ; 1-byte value type
 * 0x0001: 0x02                 ; number of object properties
 * 0x0009: 0x03                 ; length of properties[0].name
 * 0x0011: foo                  ; properties[0].name
 * 0x0014: <ValueType::Int>       ; 1-byte value type for properties[0]
 * 0x0015: 0x64                   ; 8-byte integer value
 * 0x001d: 0x03                 ; length of properties[1].name
 * 0x0025: bar                  ; properties[1].name
 * 0x0028: <ValueType::Array>     ; 1-byte value type for properties[1]
 * 0x0029: 0x03                   ; number of array items
 * 0x0031: <ValueType::Null>        ; 1-byte value type for items[0]
 * 0x0032: <ValueType::Bool>        ; 1-byte value type for items[1]
 * 0x0033: 0x01                     ; 8-byte-integer-encoded boolean value
 * 0x003b: <ValueType::String>      ; 1-byte value type for items[2]
 * 0x003c: 0x05                     ; length of string value
 * 0x0044: hello                    ; bytes of string value
 */
struct AttributeBinarySerialization {
  /**
   * Result of an attempt to serialize or parse an Attribute value to or from a file.
   */
  struct Result {
    FilesystemResult fs_result;  // Result of last filesystem read/write operation
    bool ok;                     // True if valid file was read/written in its entirety
  };

  /**
   * Given an Attribute value and a file that's been opened for write, attempts to
   * write the binary-encoded attribute value into that file at the current write
   * offset.
   *
   * If any write operation fails, `fs_result` will be set to a non-OK status indicating
   * the cause of that failure, and `ok` will always be false. Otherwise, `ok` indicates
   * whether the complete Attribute value was successfully written to the file.
   */
  static Result Write(const Attribute& attr, File& file);

  /**
   * Given an input file that's been opened for read, attempts to read a binary-encoded
   * attribute value from the current read offset, parsing it into an Attribute value
   * and storing the resulting data in `out_attr`.
   *
   * If any read operation fails, `fs_result` will be set to a non-OK status, and `ok`
   * will be false. Otherwise, `ok` indicates whether a complete, well-formed
   * `Attribute` value was successfully read from the file and stored in `out_attr`.
   *
   * `out_attr` may be left in a partially-updated state if parsing fails; the resulting
   * value should only be used when `ok` is true.
   *
   * An otherwise well-formed, properly-encoded Attribute value will be rejected if it:
   *
   * - Exceeds 64 levels of nesting in array and/or object values
   * - Contains any string value whose length exceeds 65535 bytes
   * - Contains any property name whose length exceeds 65535 bytes
   * - Contains any array value that holds more than 4096 items
   * - Contains any object value that holds more than 4096 properties
   */
  static Result Parse(File& file, Attribute& out_attr);

 private:
  /**
   * Implements `Parse` with a limit on recursion depth.
   */
  static Result ParseImpl(File& file, Attribute& out_attr, size_t depth);
};

}  // namespace datadog::impl
