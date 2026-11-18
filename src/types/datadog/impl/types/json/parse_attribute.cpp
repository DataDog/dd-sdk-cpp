// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/parse_attribute.hpp"

#include <string>

#include "datadog/impl/types/json/json_scanner.hpp"
#include "datadog/impl/types/json/parse_primitives.hpp"

namespace datadog::impl {

static bool ParseJsonNumber(std::string_view literal, Attribute& out) {
  // If the literal contains '.', 'e', or 'E', parse as double
  if (literal.find_first_of(".eE") != std::string_view::npos) {
    double val{};
    if (!ParseJsonDouble(literal, val)) {
      return false;
    }
    out = Attribute::Double(val);
    return true;
  }

  // Try int64 first, then uint64
  {
    int64_t val{};
    if (ParseJsonInt64(literal, val)) {
      out = Attribute::Int(val);
      return true;
    }
  }
  {
    uint64_t val{};
    if (ParseJsonUInt64(literal, val)) {
      out = Attribute::UInt(val);
      return true;
    }
  }

  // Not a valid number format, or lies outside the range [UINT64_MAX..INT64_MAX]
  return false;
}

static bool ParseJsonArray(std::string_view json_value, Attribute& out) {
  JsonScanner arr{json_value};
  arr.Advance();  // skip '['

  Attribute result = Attribute::Array();
  while (arr.OK() && arr.Peek() != ']') {
    auto item_span = arr.SkipValue();
    if (!arr.OK() || !item_span.OK()) {
      return false;
    }
    Attribute item;
    if (!ParseJsonAttribute(json_value.substr(item_span.i, item_span.len), item)) {
      return false;
    }
    result.ArrayPush(item);

    // After each element, the next character must be ',' or ']'; anything else
    // (including an adjacent value with no comma) is invalid JSON
    if (arr.Peek() == ',') {
      arr.Advance();
      if (arr.Peek() == ']') {
        return false;  // trailing comma
      }
    } else if (arr.Peek() != ']') {
      return false;  // missing delimiter between elements
    }
  }
  if (!arr.OK() || arr.Peek() != ']') {
    return false;
  }
  arr.Advance();  // skip ']'
  // Reject any trailing bytes after the closing bracket
  if (arr.pos != json_value.size()) {
    return false;
  }
  out = result;
  return true;
}

static bool ParseJsonObject(std::string_view json_value, Attribute& out) {
  JsonScanner obj{json_value};
  if (!obj.EnterObject()) {
    return false;
  }

  Attribute result = Attribute::Object();
  while (obj.OK() && obj.Peek() != '}') {
    auto key_span = obj.SkipStringLiteral();
    if (!obj.OK() || !key_span.OK()) {
      return false;
    }
    std::string key;
    if (!ParseJsonString(json_value.substr(key_span.i, key_span.len), key)) {
      return false;
    }

    if (obj.Peek() != ':') {
      return false;
    }
    obj.Advance();

    auto val_span = obj.SkipValue();
    if (!obj.OK() || !val_span.OK()) {
      return false;
    }
    Attribute val;
    if (!ParseJsonAttribute(json_value.substr(val_span.i, val_span.len), val)) {
      return false;
    }
    result.SetObjectProperty(key, val);

    // After each property, the next character must be ',' or '}'; anything else
    // (including a missing comma between properties) is invalid JSON
    if (obj.Peek() == ',') {
      obj.Advance();
      if (obj.Peek() == '}') {
        return false;  // trailing comma
      }
    } else if (obj.Peek() != '}') {
      return false;  // missing delimiter between properties
    }
  }
  if (!obj.OK() || obj.Peek() != '}') {
    return false;
  }
  obj.Advance();  // skip '}'
  // Reject any trailing bytes after the closing brace
  if (obj.pos != json_value.size()) {
    return false;
  }
  out = result;
  return true;
}

bool ParseJsonAttribute(std::string_view json_value, Attribute& out) {
  JsonScanner scanner{json_value};
  const char c = scanner.Peek();

  if (c == 'n') {
    auto span = scanner.SkipNameLiteral("null");
    if (!scanner.OK() || !span.OK() || scanner.pos != json_value.size()) {
      return false;
    }
    out = Attribute::Null();
    return true;
  }

  if (c == 't' || c == 'f') {
    auto span = scanner.SkipBoolLiteral();
    if (!scanner.OK() || !span.OK() || scanner.pos != json_value.size()) {
      return false;
    }
    bool val{};
    if (!ParseJsonBool(json_value.substr(span.i, span.len), val)) {
      return false;
    }
    out = Attribute::Bool(val);
    return true;
  }

  if (c == '-' || (c >= '0' && c <= '9')) {
    auto span = scanner.SkipNumberLiteral();
    if (!scanner.OK() || !span.OK() || scanner.pos != json_value.size()) {
      return false;
    }
    return ParseJsonNumber(json_value.substr(span.i, span.len), out);
  }

  if (c == '"') {
    auto span = scanner.SkipStringLiteral();
    if (!scanner.OK() || !span.OK() || scanner.pos != json_value.size()) {
      return false;
    }
    std::string val;
    if (!ParseJsonString(json_value.substr(span.i, span.len), val)) {
      return false;
    }
    out = Attribute::String(val);
    return true;
  }

  if (c == '[') {
    return ParseJsonArray(json_value, out);
  }

  if (c == '{') {
    return ParseJsonObject(json_value, out);
  }

  return false;
}

}  // namespace datadog::impl
