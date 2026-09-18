#include "browser/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cad {
namespace {

void AppendCodepointUtf8(std::string* out, uint32_t cp) {
  if (cp <= 0x7F) {
    out->push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

class Parser {
 public:
  Parser(std::string_view text, std::string* error)
      : text_(text), error_(error) {}

  JsonValue ParseDocument() {
    SkipWhitespace();
    JsonValue value = ParseValue(0);
    if (!ok_)
      return JsonValue();
    SkipWhitespace();
    if (pos_ != text_.size()) {
      Fail("trailing characters after the top-level value");
      return JsonValue();
    }
    return value;
  }

  bool ok() const { return ok_; }

 private:
  static constexpr int kMaxDepth = 64;

  void Fail(const std::string& message) {
    if (ok_ && error_)
      *error_ = message + " at offset " + std::to_string(pos_);
    ok_ = false;
  }

  void SkipWhitespace() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        ++pos_;
      else
        break;
    }
  }

  bool Consume(char expected) {
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool ConsumeLiteral(std::string_view literal) {
    if (text_.compare(pos_, literal.size(), literal) == 0) {
      pos_ += literal.size();
      return true;
    }
    return false;
  }

  JsonValue ParseValue(int depth) {
    if (depth > kMaxDepth) {
      Fail("nesting too deep");
      return JsonValue();
    }
    SkipWhitespace();
    if (pos_ >= text_.size()) {
      Fail("unexpected end of input");
      return JsonValue();
    }

    switch (text_[pos_]) {
      case '{': return ParseObject(depth);
      case '[': return ParseArray(depth);
      case '"': {
        std::string out;
        if (!ParseString(&out))
          return JsonValue();
        return JsonValue::String(std::move(out));
      }
      case 't':
        if (ConsumeLiteral("true"))
          return JsonValue::Bool(true);
        Fail("invalid literal");
        return JsonValue();
      case 'f':
        if (ConsumeLiteral("false"))
          return JsonValue::Bool(false);
        Fail("invalid literal");
        return JsonValue();
      case 'n':
        if (ConsumeLiteral("null"))
          return JsonValue();
        Fail("invalid literal");
        return JsonValue();
      default:
        return ParseNumber();
    }
  }

  JsonValue ParseObject(int depth) {
    JsonValue object = JsonValue::Object();
    ++pos_;  // '{'
    SkipWhitespace();
    if (Consume('}'))
      return object;

    for (;;) {
      SkipWhitespace();
      std::string key;
      if (!ParseString(&key))
        return JsonValue();
      SkipWhitespace();
      if (!Consume(':')) {
        Fail("expected ':' after object key");
        return JsonValue();
      }
      JsonValue value = ParseValue(depth + 1);
      if (!ok_)
        return JsonValue();
      object.Set(std::move(key), std::move(value));

      SkipWhitespace();
      if (Consume(','))
        continue;
      if (Consume('}'))
        return object;
      Fail("expected ',' or '}' in object");
      return JsonValue();
    }
  }

  JsonValue ParseArray(int depth) {
    JsonValue array = JsonValue::Array();
    ++pos_;  // '['
    SkipWhitespace();
    if (Consume(']'))
      return array;

    for (;;) {
      JsonValue value = ParseValue(depth + 1);
      if (!ok_)
        return JsonValue();
      array.Append(std::move(value));

      SkipWhitespace();
      if (Consume(','))
        continue;
      if (Consume(']'))
        return array;
      Fail("expected ',' or ']' in array");
      return JsonValue();
    }
  }

  bool ParseString(std::string* out) {
    if (!Consume('"')) {
      Fail("expected a string");
      return false;
    }
    out->clear();
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"')
        return true;
      if (c != '\\') {
        out->push_back(c);
        continue;
      }
      if (pos_ >= text_.size())
        break;
      const char escape = text_[pos_++];
      switch (escape) {
        case '"': out->push_back('"'); break;
        case '\\': out->push_back('\\'); break;
        case '/': out->push_back('/'); break;
        case 'b': out->push_back('\b'); break;
        case 'f': out->push_back('\f'); break;
        case 'n': out->push_back('\n'); break;
        case 'r': out->push_back('\r'); break;
        case 't': out->push_back('\t'); break;
        case 'u': {
          uint32_t cp = 0;
          if (!ParseHex4(&cp))
            return false;
          // A surrogate pair encodes one codepoint above the BMP.
          if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() &&
              text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
            pos_ += 2;
            uint32_t low = 0;
            if (!ParseHex4(&low))
              return false;
            if (low >= 0xDC00 && low <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else {
              AppendCodepointUtf8(out, 0xFFFD);
              cp = low;
            }
          }
          AppendCodepointUtf8(out, cp);
          break;
        }
        default:
          Fail("invalid escape sequence");
          return false;
      }
    }
    Fail("unterminated string");
    return false;
  }

  bool ParseHex4(uint32_t* out) {
    if (pos_ + 4 > text_.size()) {
      Fail("truncated \\u escape");
      return false;
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9')
        value |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f')
        value |= static_cast<uint32_t>(10 + c - 'a');
      else if (c >= 'A' && c <= 'F')
        value |= static_cast<uint32_t>(10 + c - 'A');
      else {
        Fail("invalid hex digit in \\u escape");
        return false;
      }
    }
    *out = value;
    return true;
  }

  JsonValue ParseNumber() {
    const size_t start = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+'))
      ++pos_;
    bool any_digit = false;
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c >= '0' && c <= '9') {
        any_digit = true;
        ++pos_;
      } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
        ++pos_;
      } else {
        break;
      }
    }
    if (!any_digit) {
      Fail("expected a value");
      return JsonValue();
    }
    const std::string token(text_.substr(start, pos_ - start));
    return JsonValue::Number(std::strtod(token.c_str(), nullptr));
  }

  std::string_view text_;
  std::string* error_ = nullptr;
  size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace

JsonValue JsonValue::Bool(bool value) {
  JsonValue v;
  v.type_ = Type::kBool;
  v.bool_ = value;
  return v;
}

JsonValue JsonValue::Number(double value) {
  JsonValue v;
  v.type_ = Type::kNumber;
  v.number_ = value;
  return v;
}

JsonValue JsonValue::Int(int64_t value) {
  return Number(static_cast<double>(value));
}

JsonValue JsonValue::String(std::string value) {
  JsonValue v;
  v.type_ = Type::kString;
  v.string_ = std::move(value);
  return v;
}

JsonValue JsonValue::Array() {
  JsonValue v;
  v.type_ = Type::kArray;
  return v;
}

JsonValue JsonValue::Object() {
  JsonValue v;
  v.type_ = Type::kObject;
  return v;
}

const JsonValue* JsonValue::Find(std::string_view key) const {
  if (type_ != Type::kObject)
    return nullptr;
  for (const auto& [name, value] : object_) {
    if (name == key)
      return &value;
  }
  return nullptr;
}

std::string JsonValue::GetString(std::string_view key,
                                 std::string fallback) const {
  const JsonValue* value = Find(key);
  return value && value->is_string() ? value->string_ : fallback;
}

double JsonValue::GetDouble(std::string_view key, double fallback) const {
  const JsonValue* value = Find(key);
  return value && value->is_number() ? value->number_ : fallback;
}

int64_t JsonValue::GetInt(std::string_view key, int64_t fallback) const {
  const JsonValue* value = Find(key);
  if (!value || !value->is_number())
    return fallback;
  return static_cast<int64_t>(value->number_);
}

bool JsonValue::GetBool(std::string_view key, bool fallback) const {
  const JsonValue* value = Find(key);
  return value && value->is_bool() ? value->bool_ : fallback;
}

const JsonValue* JsonValue::GetChildArray(std::string_view key) const {
  const JsonValue* value = Find(key);
  return value && value->is_array() ? value : nullptr;
}

const JsonValue* JsonValue::GetChildObject(std::string_view key) const {
  const JsonValue* value = Find(key);
  return value && value->is_object() ? value : nullptr;
}

void JsonValue::Set(std::string key, JsonValue value) {
  if (type_ != Type::kObject) {
    type_ = Type::kObject;
    object_.clear();
  }
  for (auto& [name, existing] : object_) {
    if (name == key) {
      existing = std::move(value);
      return;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
}

void JsonValue::Append(JsonValue value) {
  if (type_ != Type::kArray) {
    type_ = Type::kArray;
    array_.clear();
  }
  array_.push_back(std::move(value));
}

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          ::_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "\\u%04x",
                        static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buffer;
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

void JsonValue::SerializeTo(std::string* out, bool pretty, int depth) const {
  const std::string indent = pretty ? std::string(size_t(depth + 1) * 2, ' ') : "";
  const std::string closing_indent =
      pretty ? std::string(size_t(depth) * 2, ' ') : "";
  const char* separator = pretty ? ",\n" : ",";
  const char* colon = pretty ? ": " : ":";

  switch (type_) {
    case Type::kNull:
      *out += "null";
      break;
    case Type::kBool:
      *out += bool_ ? "true" : "false";
      break;
    case Type::kNumber: {
      if (!std::isfinite(number_)) {
        *out += "0";
        break;
      }
      // Integral values print without a decimal point so that ids and counts
      // in the session file stay readable.
      if (number_ == std::floor(number_) && std::fabs(number_) < 1e15) {
        *out += std::to_string(static_cast<int64_t>(number_));
      } else {
        char buffer[40];
        ::_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%.17g", number_);
        *out += buffer;
      }
      break;
    }
    case Type::kString:
      *out += JsonEscape(string_);
      break;
    case Type::kArray: {
      if (array_.empty()) {
        *out += "[]";
        break;
      }
      *out += pretty ? "[\n" : "[";
      for (size_t i = 0; i < array_.size(); ++i) {
        if (i > 0)
          *out += separator;
        *out += indent;
        array_[i].SerializeTo(out, pretty, depth + 1);
      }
      *out += pretty ? "\n" + closing_indent + "]" : "]";
      break;
    }
    case Type::kObject: {
      if (object_.empty()) {
        *out += "{}";
        break;
      }
      *out += pretty ? "{\n" : "{";
      for (size_t i = 0; i < object_.size(); ++i) {
        if (i > 0)
          *out += separator;
        *out += indent;
        *out += JsonEscape(object_[i].first);
        *out += colon;
        object_[i].second.SerializeTo(out, pretty, depth + 1);
      }
      *out += pretty ? "\n" + closing_indent + "}" : "}";
      break;
    }
  }
}

std::string JsonValue::Serialize(bool pretty) const {
  std::string out;
  SerializeTo(&out, pretty, 0);
  return out;
}

JsonValue JsonValue::Parse(std::string_view text, std::string* error) {
  if (error)
    error->clear();

  // Skip a UTF-8 byte order mark. settings.json and session.json are meant to
  // be editable by hand, and Notepad, PowerShell's Set-Content and most
  // Windows editors write one. Treating it as a syntax error would silently
  // reset every setting the moment someone opened the file and saved it.
  constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
  if (text.size() >= kUtf8Bom.size() &&
      text.compare(0, kUtf8Bom.size(), kUtf8Bom) == 0) {
    text.remove_prefix(kUtf8Bom.size());
  }

  Parser parser(text, error);
  JsonValue value = parser.ParseDocument();
  if (!parser.ok())
    return JsonValue();
  return value;
}

}  // namespace cad
