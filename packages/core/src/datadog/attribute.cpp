// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/attribute.h"

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

// ----------
// CowStorage
// ----------
DatadogAttribute::CowStorage::CowStorage(const CowStorage& old)
    : type_(old.type_) {
  switch (type_) {
    case Type::String:
      // TODO
      break;
    case Type::Array:
      Reserve(old.array_value.size);
      for (uint32_t i = 0; i < old.array_value.size; ++i) {
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
  // TODO(jeff.ward): Copy old items, delete previous memory
  if (array_value.size < size) {
    array_value.values = new DatadogAttribute[size];
    array_value.size = size;
  }
}

}  // namespace datadog::core
