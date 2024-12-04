// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/attribute.h"

#include "datadog/internal/utils.h"

namespace datadog::core {

const DatadogAttribute DatadogAttribute::kNull{DatadogAttribute::Type::Null};

DatadogAttribute::~DatadogAttribute() {}

void DatadogAttribute::Detach() {
  if (cow_data_ && !cow_data_.unique()) {
    // Hold a strong copy while copying to prevent a threaded deletion
    auto old = cow_data_;
    cow_data_ = std::make_shared<CowStorage>(*old.get());
  }
}

void DatadogAttribute::Reserve(uint32_t new_capacity) {
  // Don't allow reserve of non-arrays
  if (type_ != Type::Array) {
    return;
  }

  cow_data_->Reserve(new_capacity);
}

int64_t DatadogAttribute::IntValue() const {
  switch (type_) {
    case Type::Int:
      return prim_.int_value;
    case Type::UInt:
      return static_cast<int64_t>(prim_.uint_value);
    case Type::Double:
      return static_cast<int64_t>(prim_.double_value);
    default:
      return 0;
  }
}

uint64_t DatadogAttribute::UIntValue() const {
  switch (type_) {
    case Type::Int: {
      auto value = prim_.int_value;
      if (value < 0) {
        return 0;
      }
      return static_cast<uint64_t>(value);
    }
    case Type::UInt:
      return prim_.uint_value;
    case Type::Double: {
      auto value = prim_.double_value;
      if (value < 0.0) {
        return 0.0;
      }
      return static_cast<uint64_t>(value);
    }
    default:
      return 0u;
  }
}

double DatadogAttribute::DoubleValue() const {
  switch (type_) {
    case Type::Int:
      return static_cast<double>(prim_.int_value);
    case Type::UInt:
      return static_cast<double>(prim_.uint_value);
    case Type::Double:
      return prim_.double_value;
    default:
      return 0.0;
  }
}

std::string_view DatadogAttribute::StringValue() const {
  if (type_ != Type::String) {
    return std::string_view("");
  }

  return std::string_view(cow_data_->string_value.str,
                          cow_data_->string_value.length);
}

// ----------
// CowStorage
// ----------
DatadogAttribute::CowStorage::CowStorage(const CowStorage& old)
    : type_(old.type_), object_value{0, 0, nullptr} {
  switch (type_) {
    case Type::String:
      // TODO
      break;
    case Type::Array:
      Reserve(old.array_value.size);
      for (uint32_t i = 0; i < old.array_value.size; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        array_value.values[i] = old.array_value.values[i];
      }
      break;
    default:
      break;
  }
}

DatadogAttribute::CowStorage::~CowStorage() {
  switch (type_) {
    case Type::String:
      delete string_value.str;
      break;
    case Type::Array:
      delete[] array_value.values;
      break;
    case Type::Object:
      delete[] object_value.members;
      break;
    default:
      break;
  }
}

void DatadogAttribute::CowStorage::Reserve(uint32_t size) {
  // Use of pointer / array access and owner swaps here is unavoidable
  // due to the use of the union (which won't allow any smart pointer types)
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  // NOLINTBEGIN(cppcoreguidelines-owning-memory)
  if (type_ == Type::Array) {
    if (array_value.size < size) {
      auto old_values = array_value.values;
      array_value.values = new DatadogAttribute[size];
      if (old_values) {
        for (uint32_t i = 0; i < array_value.size; ++i) {
          array_value.values[i] = old_values[i];
        }
        delete[] old_values;
      }
      array_value.size = size;
    }
  } else if (type_ == Type::Object) {
  }
  // NOLINTEND(cppcoreguidelines-owning-memory)
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

}  // namespace datadog::core
