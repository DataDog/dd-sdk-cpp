// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include <cstdint>
#include <cstring>
#include <memory>

namespace datadog::core {

namespace internal {
struct ObjectMember;
}

// DatadogAttribute is a JSON-like value with Copy on Write semantics.
//
// DatadogAttribute is optimized for storing, full serialization, and
// "freezing" information, but not for frequent writes or for reading arbitrary
// information out. This is specific for Datadog's use case for attributes,
// where we need to capture attributes at a specific time, but clients do not
// want the overhead of copying or serializing attributes, and they also do not
// need to read the information they set.
//
// DatadogAttributes are not guarenteed to be thread safe, and a given attribute
// should not be modified from multiple threads.
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

  // Create an attribute that should hold the supplied type, with an optional
  // reserved size.
  explicit DatadogAttribute(Type type, uint32_t size = 0) : type_(type) {
    if (IsCowType(type_)) {
      cow_data_ = std::make_shared<CowStorage>(type);
      Reserve(size);
    }
  }

  explicit DatadogAttribute(std::string_view str) { SetValue(str); }

  // Create an attribute with the given integral or floating point value.
  template <
      typename T,
      std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>,
                       int> = 0>
  explicit DatadogAttribute(T value) {
    SetValue(value);
  }
  ~DatadogAttribute();

  Type type() const { return type_; }

  // Return the Integer value stored in this attribute.
  // If the attribute is an unsigned integer, it will be cast to a signed
  // integer. If the attribute is a double, it will be truncated. All other
  // types wil return zero.
  int64_t IntValue() const;

  // Return the Unsigned Integer value stored in this attribute.
  // If the attribute is a double or signed integer below zero, this will return
  // zero. If the attribute is a double above zero, it will be truncated.
  uint64_t UIntValue() const;

  // Return the Double value stored in this attribute.
  // If the attribute is an integer or unsigned integer, it will be converted to
  // a double.
  double DoubleValue() const;

  // Return the string value stored in this attribute.
  // If the attribute is not a string, an empty string will be returned.
  std::string_view StringValue() const;

  // Set the value of this attribute (for signed integral types)
  template <
      typename T,
      std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, int> = 0>
  void SetValue(T value) {
    type_ = Type::Int;
    cow_data_ = nullptr;
    prim_.int_value = static_cast<int64_t>(value);
  }

  // Set the value of this attribute (for unsigned integral types)
  template <
      typename T,
      std::enable_if_t<std::is_integral_v<T> && !std::is_signed_v<T>, int> = 0>
  void SetValue(T value) {
    type_ = Type::UInt;
    cow_data_ = nullptr;
    prim_.int_value = static_cast<uint64_t>(value);
  }

  // Set the value of this attribute (for floating point types)
  template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
  void SetValue(T value) {
    type_ = Type::Double;
    cow_data_ = nullptr;
    prim_.double_value = value;
  }

  // Set the string value of this attribute
  void SetValue(std::string_view str) {
    if (this == &kNull) {
      return;
    }
    type_ = Type::String;
    cow_data_ = std::make_shared<CowStorage>(Type::String);
    char* str_copy = new char[str.length()];
    str.copy(str_copy, str.length(), 0);
    cow_data_->string_value.length = str.length();
    cow_data_->string_value.str = str_copy;
  }

  // Reserve a number of elements for this attribute to hold. Reserving
  // space only works for attributes that hold Arrays and Objects
  void Reserve(uint32_t size);

  // Set the attribute at the given index of the array.
  // If this attribute is not an array or is smaller than the requested index,
  // this method silently fails.
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

  // Get the attribute at the given index of the array.
  // If this attribute is not an array or is smaller than the requested index,
  // this method return kNull.
  const DatadogAttribute& ArrayGetAt(uint32_t index) {
    if (type_ != Type::Array) {
      return kNull;
    }
    if (index >= cow_data_->array_value.size) {
      return kNull;
    }

    return cow_data_->array_value.values[index];
  }

  // Set the value for the given object's member.
  // If this attribute is not an object, this method will silently fail.
  // If there is insufficient room in the object for this member, the memory
  // allocated for this object will be expanded by 1.
  void SetMember(std::string_view member_name, const DatadogAttribute& value);

  // Get the value for the given object's member.
  // If this attribute is not an object or the member does not exist, this
  // method will return kNull.
  const DatadogAttribute& GetMember(std::string_view member_name) const;

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
    internal::ObjectMember* members;
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

namespace internal {
// Must be declared outside of Datadog Attribute so we can store a
// DatadogAttribute inside it (instead of a pointer).
struct ObjectMember {
  // TODO(RUM-): Optimize member lookup potentially by storing a hash of the
  // member name.
  std::string name;
  DatadogAttribute value;
};
}  // namespace internal

}  // namespace datadog::core
