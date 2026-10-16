// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/attribute/binary.hpp"

#include <cmath>
#include <limits>
#include <optional>

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/types/json.hpp"

#include "mock/filesystem.hpp"
#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("AttributeBinarySerialization", "[unit][attribute]") {
  // Given a mock filesystem that we can use to read and write in-memory files
  MockFilesystem fs;
  fs.Mkdirs("tmp");
  FilesystemWrapper fsw(fs);

  // And a couple of helper functions that will Write and Parse to/from a mock file
  auto encode = [&fsw](const Attribute& attr) -> bool {
    auto open_res = fsw.OpenForWrite("tmp/foo", false, false);
    REQUIRE(open_res.value == FilesystemResult::OK);
    auto res = AttributeBinarySerialization::Write(attr, open_res.file);
    REQUIRE(res.fs_result == FilesystemResult::OK);
    return res.ok;
  };
  auto decode = [&fsw]() -> std::optional<Attribute> {
    auto open_res = fsw.OpenForRead("tmp/foo", false);
    REQUIRE(open_res.value == FilesystemResult::OK);
    Attribute result;
    auto res = AttributeBinarySerialization::Parse(open_res.file, result);
    REQUIRE(res.fs_result == FilesystemResult::OK);
    return res.ok ? std::optional{result} : std::nullopt;
  };

  SECTION("M encode and decode values faithfully") {
    // Given a helper that will flush an Attribute value to disk, then parse it back
    auto write_then_parse = [&](const Attribute& attr) -> Attribute {
      const bool encoded_ok = encode(attr);
      REQUIRE(encoded_ok);
      const auto attr_opt = decode();
      REQUIRE(attr_opt.has_value());
      return *attr_opt;
    };

    // When we round-trip an Attribute value of a given type to disk and back, the value
    // that we parse is equivalent to the original value we serialized

    SECTION("{Null}") {
      auto attr = write_then_parse(Attribute::Null());
      REQUIRE(attr.GetType() == ValueType::Null);
    }

    SECTION("{Bool}") {
      auto value = GENERATE(false, true);
      auto attr = write_then_parse(Attribute::Bool(value));
      REQUIRE(attr.GetType() == ValueType::Bool);
      REQUIRE(attr.GetBoolValue() == value);
    }

    SECTION("{Int}") {
      auto value = GENERATE(
          0ll,
          -42ll,
          101ll,
          std::numeric_limits<int64_t>::min(),
          std::numeric_limits<int64_t>::max()
      );
      auto attr = write_then_parse(Attribute::Int(value));
      REQUIRE(attr.GetType() == ValueType::Int);
      REQUIRE(attr.GetIntValue() == value);
    }

    SECTION("{UInt}") {
      auto value = GENERATE(0ull, 101ull, std::numeric_limits<uint64_t>::max());
      auto attr = write_then_parse(Attribute::UInt(value));
      REQUIRE(attr.GetType() == ValueType::UInt);
      REQUIRE(attr.GetUIntValue() == value);
    }

    SECTION("{Timestamp}") {
      auto value = GENERATE(
          Timestamp{std::chrono::nanoseconds{0}},
          Timestamp{std::chrono::hours{400000}},
          Timestamp{std::chrono::nanoseconds{std::numeric_limits<int64_t>::max()}}
      );
      auto attr = write_then_parse(Attribute::Timestamp(value));
      REQUIRE(attr.GetType() == ValueType::Timestamp);
      REQUIRE(attr.GetTimestampValue() == value);
    }

    SECTION("{Double}") {
      auto value = GENERATE(
          0.0,
          -42.0,
          867.5309,
          std::numeric_limits<double>::min(),
          std::numeric_limits<double>::max(),
          std::numeric_limits<double>::lowest(),
          std::numeric_limits<double>::denorm_min(),
          std::numeric_limits<double>::epsilon(),
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()
      );
      auto attr = write_then_parse(Attribute::Double(value));
      REQUIRE(attr.GetType() == ValueType::Double);
      if (std::isnan(value)) {
        REQUIRE(std::isnan(attr.GetDoubleValue()));
      } else {
        REQUIRE(attr.GetDoubleValue() == value);
      }
    }

    SECTION("{UUID}") {
      auto value = GENERATE(
          UUID::Zero,
          *UUID::Parse("feeeeeee-deee-ceee-beee-a12345678900"),
          *UUID::Parse("230dbff9-48fc-4840-b0a4-0981a3751b73")
      );
      auto attr = write_then_parse(Attribute::UUID(value.bytes.data()));
      REQUIRE(attr.GetType() == ValueType::UUID);
      REQUIRE(attr.GetUUIDValue() == value);
    }

    SECTION("{String}") {
      auto value = GENERATE(
          "",
          "hello",
          "this string\\ is long and contains\n characters \u0007 that would need to "
          "be ⚠️ escaped ⚠️ when serializing to JSON, but this ain't JSON"
      );
      auto attr = write_then_parse(Attribute::String(value));
      REQUIRE(attr.GetType() == ValueType::String);
      REQUIRE(attr.GetStringValue() == value);
    }

    SECTION("{Array}") {
      SECTION("{len=0}") {
        auto attr = write_then_parse(Attribute::Array(0));
        REQUIRE(attr.GetType() == ValueType::Array);
        REQUIRE(attr.GetArrayLen() == 0);
      }
      SECTION("{len=3}") {
        auto arr = Attribute::Array(3);
        arr.ArrayPush(Attribute::Int(100));
        arr.ArrayPush(Attribute::String("foo"));
        arr.ArrayPush(Attribute::Array(0));
        auto attr = write_then_parse(arr);
        REQUIRE(attr.GetType() == ValueType::Array);
        REQUIRE(attr.GetArrayLen() == 3);
        REQUIRE(attr.GetArrayItem(0).GetIntValue() == 100);
        REQUIRE(attr.GetArrayItem(1).GetStringValue() == "foo");
        REQUIRE(attr.GetArrayItem(2).GetType() == ValueType::Array);
        REQUIRE(attr.GetArrayItem(2).GetArrayLen() == 0);
      }
    }

    SECTION("{Object}") {
      SECTION("{len=0}") {
        auto attr = write_then_parse(Attribute::Object(0));
        REQUIRE(attr.GetType() == ValueType::Object);
        REQUIRE(attr.GetObjectPropertyCount() == 0);
      }
      SECTION("{len=3}") {
        auto obj = Attribute::Object(3);
        obj.SetObjectProperty("alpha", Attribute::Int(100));
        obj.SetObjectProperty("bravo", Attribute::String("foo"));
        obj.SetObjectProperty("charlie", Attribute::Object(0));
        auto attr = write_then_parse(obj);
        REQUIRE(attr.GetType() == ValueType::Object);
        REQUIRE(attr.GetObjectPropertyCount() == 3);
        REQUIRE(attr.GetObjectPropertyNameAt(0) == "alpha");
        REQUIRE(attr.GetObjectPropertyNameAt(1) == "bravo");
        REQUIRE(attr.GetObjectPropertyNameAt(2) == "charlie");
        REQUIRE(attr.GetObjectProperty("alpha").GetIntValue() == 100);
        REQUIRE(attr.GetObjectProperty("bravo").GetStringValue() == "foo");
        REQUIRE(attr.GetObjectProperty("charlie").GetType() == ValueType::Object);
        REQUIRE(attr.GetObjectProperty("charlie").GetObjectPropertyCount() == 0);
      }
    }

    SECTION("{complex nested value}") {
      // Given our reference Attribute object:
      /*
        {
            "process": {
                "pid": 9238451,
                "guid": "ccb79084-bc2b-4549-bbc7-f27e153fd4b6",
                "name": "my-cool-program",
                "args": ["--mode", "good"],
                "started_at": "1974-08-09T16:00:00.000Z"
            },
            "state": {
                "rect": [[0.03333, -12.3], [94, 98.7001]],
                "state": [{}],
                "offset": -1,
                "active": true
            },
            "tags": ["blue", "meh", null]
        }
      */
      Attribute obj = Attribute::Object(3);
      {
        Attribute args = Attribute::Array(2);
        args.ArrayPush(Attribute::String("--mode"));
        args.ArrayPush(Attribute::String("good"));

        Attribute process = Attribute::Object(5);
        process.SetObjectProperty("pid", Attribute::UInt(9238451));
        static const uint8_t bytes_ccb7[16] = {
            204, 183, 144, 132, 188, 43, 69, 73, 187, 199, 242, 126, 21, 63, 212, 182
        };
        process.SetObjectProperty("guid", Attribute::UUID(bytes_ccb7));
        process.SetObjectProperty("name", Attribute::String("my-cool-program"));
        process.SetObjectProperty("args", args);
        process.SetObjectProperty(
            "started_at",
            Attribute::Timestamp(Timestamp(std::chrono::seconds(145296000)))
        );

        obj.SetObjectProperty("process", process);
      }
      {
        Attribute state = Attribute::Object(4);
        {
          Attribute coord_0 = Attribute::Array(2);
          coord_0.ArrayPush(Attribute::Double(0.03333));
          coord_0.ArrayPush(Attribute::Double(-12.3));
          Attribute coord_1 = Attribute::Array(2);
          coord_1.ArrayPush(Attribute::Double(94.0));
          coord_1.ArrayPush(Attribute::Double(98.7001));

          Attribute rect = Attribute::Array(2);
          rect.ArrayPush(coord_0);
          rect.ArrayPush(coord_1);

          state.SetObjectProperty("rect", rect);
        }
        {
          Attribute inner_state = Attribute::Array(1);
          inner_state.ArrayPush(Attribute::Object());
          state.SetObjectProperty("state", inner_state);
        }
        state.SetObjectProperty("offset", Attribute::Int(-1));
        state.SetObjectProperty("active", Attribute::Bool(true));

        obj.SetObjectProperty("state", state);
      }
      {
        Attribute tags = Attribute::Array(3);
        tags.ArrayPush(Attribute::String("blue"));
        tags.ArrayPush(Attribute::String("meh"));
        tags.ArrayPush(Attribute::Null());
        obj.SetObjectProperty("tags", tags);
      }

      // When we serialize this complex value to a file and then read it back
      auto attr = write_then_parse(obj);

      // Then the original Attribute value and the re-parsed Attribute value produce
      // identical JSON payloads
      std::vector<uint8_t> want_buf;
      EncodeJson(want_buf, obj);
      std::string_view want{reinterpret_cast<char*>(want_buf.data()), want_buf.size()};

      std::vector<uint8_t> got_buf;
      EncodeJson(got_buf, attr);
      std::string_view got{reinterpret_cast<char*>(got_buf.data()), got_buf.size()};

      REQUIRE(want == got);
    }
  }

  SECTION("M reject invalid input data") {
    static std::string str_over_limit(65536, 'A');
    static std::string_view str_at_limit(
        str_over_limit.data(), str_over_limit.size() - 1
    );

    SECTION("W first byte is not a valid ValueType") {
      // When file contains an invalid ValueType in its first byte, parsing fails
      char buf[8] = {};
      buf[0] = 42;
      fs.Touch("tmp/foo", std::string_view{buf, sizeof(buf)});
      REQUIRE(!decode().has_value());
    }

    SECTION("W file ends before expected value length") {
      // Every type except Null must have at least 8 bytes of data after the type
      // identifier: if we hit EOF, parsing should fail
      auto type = GENERATE(
          ValueType::Bool,
          ValueType::Int,
          ValueType::UInt,
          ValueType::Timestamp,
          ValueType::Double,
          ValueType::UUID,
          ValueType::String,
          ValueType::Array,
          ValueType::Object
      );
      char buf[3] = {};
      buf[0] = static_cast<char>(type);
      fs.Touch("tmp/foo", std::string_view{buf, sizeof(buf)});
      REQUIRE(!decode().has_value());
    }

    SECTION("W file ends before expected string value length") {
      // If a string value has a prefix indicating a length of 256, but we hit EOF
      // before reading 256 bytes of string data, parsing should fail
      char buf[128] = {};
      buf[0] = static_cast<char>(ValueType::String);
      const uint64_t str_len = 256;
      memcpy(&buf[1], &str_len, sizeof(str_len));
      fs.Touch("tmp/foo", std::string_view{buf, sizeof(buf)});
      REQUIRE(!decode().has_value());
    }

    SECTION("W file ends before expected object property name length") {
      // If an object property name has a length prefix of 256, but we hit EOF before
      // reading 256 bytes of name data, parsing should fail
      char buf[128] = {};
      buf[0] = static_cast<char>(ValueType::Object);
      const uint64_t num_properties = 1;
      memcpy(&buf[1], &num_properties, sizeof(num_properties));
      const uint64_t name_len = 256;
      memcpy(&buf[9], &name_len, sizeof(name_len));
      fs.Touch("tmp/foo", std::string_view{buf, sizeof(buf)});
      REQUIRE(!decode().has_value());
    }

    SECTION("W string value exceeds 65535 bytes") {
      // A 64kb string value can be decoded just fine
      REQUIRE(encode(Attribute::String(str_at_limit)));
      auto decoded = decode();
      REQUIRE(decoded.has_value());
      REQUIRE(decoded->GetStringValue() == str_at_limit);

      // But a string with length 64kb + 1 byte will be rejected
      REQUIRE(encode(Attribute::String(str_over_limit)));
      REQUIRE(!decode().has_value());
    }

    SECTION("W object property name exceeds 65535 bytes") {
      // A 64kb object property name can be decoded just fine
      auto obj = Attribute::Object(1);
      obj.SetObjectProperty(str_at_limit, Attribute::Int(100));
      REQUIRE(encode(obj));
      auto decoded = decode();
      REQUIRE(decoded.has_value());
      REQUIRE(decoded->GetObjectPropertyCount() == 1);
      REQUIRE(decoded->GetObjectPropertyNameAt(0) == str_at_limit);

      // But a property name with length 64kb + 1 byte will be rejected
      obj.DeleteObjectProperty(str_at_limit);
      REQUIRE(obj.GetObjectPropertyCount() == 0);
      obj.SetObjectProperty(str_over_limit, Attribute::Int(200));
      REQUIRE(encode(obj));
      REQUIRE(!decode().has_value());
    }

    SECTION("W array length exceeds 4096 items") {
      // An array with 4096 items can be decoded just fine
      auto arr = Attribute::Array(4097);
      for (int64_t i = 0; i < 4096; i++) {
        arr.ArrayPush(Attribute::Int(i));
      }
      REQUIRE(arr.GetArrayLen() == 4096);
      REQUIRE(encode(arr));
      auto decoded = decode();
      REQUIRE(decoded.has_value());
      REQUIRE(decoded->GetArrayLen() == 4096);
      REQUIRE(decoded->GetArrayItem(512).GetIntValue() == 512);

      // But an array with 4097 items is rejected
      arr.ArrayPush(Attribute::Int(4096));
      REQUIRE(arr.GetArrayLen() == 4097);
      REQUIRE(encode(arr));
      REQUIRE(!decode().has_value());
    }

    SECTION("W object exceeds 4096 properties") {
      // An object with 4096 properties can be decoded just fine
      auto obj = Attribute::Object(4097);
      for (int64_t i = 0; i < 4096; i++) {
        obj.SetObjectProperty(std::to_string(i), Attribute::Int(i));
      }
      REQUIRE(obj.GetObjectPropertyCount() == 4096);
      REQUIRE(encode(obj));
      auto decoded = decode();
      REQUIRE(decoded.has_value());
      REQUIRE(decoded->GetObjectPropertyCount() == 4096);
      REQUIRE(decoded->GetObjectProperty("512").GetIntValue() == 512);

      // But an object with 4097 properties is rejected
      obj.SetObjectProperty("4096", Attribute::Int(4096));
      REQUIRE(obj.GetObjectPropertyCount() == 4097);
      REQUIRE(encode(obj));
      REQUIRE(!decode().has_value());
    }

    SECTION("W nesting depth exceeds 64 levels") {
      // Given a helper that constructs an Attribute nested N levels deep
      auto make_nested = [](int depth) {
        Attribute attr = Attribute::Null();
        for (int i = 0; i < depth; i++) {
          Attribute arr = Attribute::Array(1);
          arr.ArrayPush(attr);
          attr = arr;
        }
        return attr;
      };

      // An attribute nested 64 levels deep can be decoded just fine
      REQUIRE(encode(make_nested(64)));
      auto decoded = decode();
      REQUIRE(decoded.has_value());
      REQUIRE(
          decoded->GetArrayItem(0)
              .GetArrayItem(0)
              .GetArrayItem(0)
              .GetArrayItem(0)
              .GetArrayItem(0)
              .GetArrayLen() == 1
      );

      // But an attribute nested 65 levels deep is rejected
      REQUIRE(encode(make_nested(65)));
      REQUIRE(!decode().has_value());
    }
  }

  SECTION("Write M propagate FilesystemResult W file I/O fails") {
    // Given a valid Attribute value
    auto attr = GENERATE(
        Attribute::Null(),
        Attribute::Bool(true),
        Attribute::Int(-101),
        Attribute::UInt(101),
        Attribute::Timestamp(Timestamp{std::chrono::milliseconds{1700000000000}}),
        Attribute::Double(1.01),
        Attribute::UUID(UUID::Zero.bytes.data()),
        Attribute::String("hello world"),
        Attribute::Array(0),
        Attribute::Object(0)
    );

    // And a filesystem that will refuse to let us write to tmp/foo
    fs.Touch("tmp/foo");
    fs.SimulateFailure(
        "tmp/foo", FilesystemResult::PermissionDenied, MockFilesystem::FailureFlags::IO
    );

    // When we open that file and attempt to serialize an Attribute value into it
    auto open_res = fsw.OpenForWrite("tmp/foo", false, false);
    REQUIRE(open_res.value == FilesystemResult::OK);
    auto res = AttributeBinarySerialization::Write(attr, open_res.file);

    // Then the call to Write() is unsuccessful
    REQUIRE(!res.ok);

    // And the result value indicates the filesystem error that caused the failure
    REQUIRE(res.fs_result == FilesystemResult::PermissionDenied);
  }

  SECTION("Parse M propagate FilesystemResult W file I/O fails") {
    // Given a valid Attribute value that's been successfully written to a mock file
    auto attr = GENERATE(
        Attribute::Null(),
        Attribute::Bool(true),
        Attribute::Int(-101),
        Attribute::UInt(101),
        Attribute::Timestamp(Timestamp{std::chrono::milliseconds{1700000000000}}),
        Attribute::Double(1.01),
        Attribute::UUID(UUID::Zero.bytes.data()),
        Attribute::String("hello world"),
        Attribute::Array(0),
        Attribute::Object(0)
    );
    REQUIRE(encode(attr));

    // And a filesystem that will refuse to let us read from that file
    fs.SimulateFailure(
        "tmp/foo", FilesystemResult::PermissionDenied, MockFilesystem::FailureFlags::IO
    );

    // When we open the file and attempt to parse an Attribute value from it
    auto open_res = fsw.OpenForRead("tmp/foo", false);
    REQUIRE(open_res.value == FilesystemResult::OK);
    Attribute result;
    auto res = AttributeBinarySerialization::Parse(open_res.file, result);

    // Then the call to Parse() is unsuccessful
    REQUIRE(!res.ok);

    // And the result value indicates the filesystem error that caused the failure
    REQUIRE(res.fs_result == FilesystemResult::PermissionDenied);
  }
}
