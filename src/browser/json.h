// CAD Browser - a small JSON reader/writer for settings and session files.
//
// Deliberately minimal rather than a dependency: the only JSON this browser
// reads is its own, both files are small, and CEF builds with exceptions
// disabled so a parser that reports errors by return value fits better than
// most third-party ones.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cad {

class JsonValue {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  JsonValue() = default;
  static JsonValue Bool(bool value);
  static JsonValue Number(double value);
  static JsonValue Int(int64_t value);
  static JsonValue String(std::string value);
  static JsonValue Array();
  static JsonValue Object();

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::kNull; }
  bool is_object() const { return type_ == Type::kObject; }
  bool is_array() const { return type_ == Type::kArray; }
  bool is_string() const { return type_ == Type::kString; }
  bool is_number() const { return type_ == Type::kNumber; }
  bool is_bool() const { return type_ == Type::kBool; }

  // ---- object access. Missing keys yield the supplied default. ----
  const JsonValue* Find(std::string_view key) const;
  std::string GetString(std::string_view key, std::string fallback = {}) const;
  double GetDouble(std::string_view key, double fallback = 0.0) const;
  int64_t GetInt(std::string_view key, int64_t fallback = 0) const;
  bool GetBool(std::string_view key, bool fallback = false) const;
  const JsonValue* GetChildArray(std::string_view key) const;
  const JsonValue* GetChildObject(std::string_view key) const;

  void Set(std::string key, JsonValue value);
  void Append(JsonValue value);

  const std::vector<JsonValue>& items() const { return array_; }
  const std::vector<std::pair<std::string, JsonValue>>& members() const {
    return object_;
  }

  double number() const { return number_; }
  bool boolean() const { return bool_; }
  const std::string& string() const { return string_; }

  std::string Serialize(bool pretty = false) const;

  // Returns a null value and sets `error` when the text is not valid JSON.
  static JsonValue Parse(std::string_view text, std::string* error = nullptr);

 private:
  void SerializeTo(std::string* out, bool pretty, int depth) const;

  Type type_ = Type::kNull;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<JsonValue> array_;
  // A vector rather than a map: settings files read better when the keys stay
  // in the order they were written.
  std::vector<std::pair<std::string, JsonValue>> object_;
};

// Escapes a string as a JSON string literal, including the surrounding quotes.
std::string JsonEscape(std::string_view text);

}  // namespace cad
