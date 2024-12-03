// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <cstdint>
#include <cstring>
#include <memory>

namespace datadog::core {

// DatadogAttribute is a JSON-like value with Copy on Write semantics.
//
// DatadogAttribute is optimized for storing, full serialization, and
// "freezing" information, but not for frequent writes or for reading arbitrary
// information out. This is specific for Datadog's use case for attributes,
// where we need to capture attributes at a specific time, but clients do not
// want the overhead of copying or serializing attributes, and they also do not
// need to read the information they set.
class DatadogAttribute {
 public:
  enum class Type {
    Null,
    Int,
    UInt,
    Double,
    String,
    Array,
    Object,
  };

  DatadogAttribute() : type_(Type::Null) {}
  constexpr explicit DatadogAttribute(Type type) : type_(type) {
    if (IsCowType(type_)) {
      cow_data_ = std::make_shared<CowStorage>(type);
    }
  }
  explicit DatadogAttribute(int64_t value) { SetValue(value); }
  ~DatadogAttribute();

  Type type() const { return type_; }

  int64_t IntValue() const { return prim_.int_value; }

  void SetValue(int64_t value) {
    Detach();
    type_ = Type::Int;
    prim_.int_value = value;
  }

  void Reserve(uint32_t size);
  void ArraySetAt(uint32_t index, DatadogAttribute attr) {
    if (type_ != Type::Array) {
      return;
    }
    if (index >= cow_data_->array_value.size) {
      return;
    }

    Detach();
    cow_data_->array_value.values[index] = attr;
  }
  const DatadogAttribute& ArrayGetAt(uint32_t index) {
    if (type_ != Type::Array) {
      return kNull;
    }
    if (index >= cow_data_->array_value.size) {
      return kNull;
    }

    return cow_data_->array_value.values[index];
  }

  static const DatadogAttribute kNull;

 private:
  struct StringStorage {
    uint32_t length;
    const char* str;
  };

  struct ArrayStorage {
    uint32_t size;
    DatadogAttribute* values;
  };

  struct ObjectStorage {
    uint32_t size;
    uint32_t capacity;
    DatadogAttribute* members;
  };

  struct PrimitiveStorage {
    constexpr PrimitiveStorage() : int_value{0} {}

    union {
      int64_t int_value;
      uint64_t uint_value;
      double double_value;
    };
  };

  // RESEARCH: CowStorage pool? Needs to use `std::allocate_shared` with a
  // custom allocator
  class CowStorage {
   public:
    constexpr CowStorage(Type type)
        : type_{type}, object_value{0, 0, nullptr} {};
    CowStorage(const CowStorage& old);
    ~CowStorage();

    void Reserve(uint32_t size);

    // Any chance we can avoid double storing this?
    Type type_;
    union {
      StringStorage string_value;
      ArrayStorage array_value;
      ObjectStorage object_value;
    };
  };

  void Detach();

  static bool IsCowType(Type type) {
    return type == Type::String || type == Type::Array || type == Type::Object;
  }

  Type type_;
  PrimitiveStorage prim_;
  std::shared_ptr<CowStorage> cow_data_;
};

}  // namespace datadog::core
