#include "cad_test.h"

#include "browser/json.h"
#include "browser/settings_store.h"

namespace {

using cad::JsonValue;
using cad::Settings;
using cad::SettingsStore;

}  // namespace

CAD_TEST(JsonParsesObjectsAndTypes) {
  std::string error;
  const JsonValue value = JsonValue::Parse(
      R"({"a":1,"b":"two","c":true,"d":null,"e":[1,2,3]})", &error);
  CAD_EXPECT_EQ(error, std::string());
  CAD_EXPECT(value.is_object());
  CAD_EXPECT_EQ(value.GetInt("a"), int64_t(1));
  CAD_EXPECT_EQ(value.GetString("b"), std::string("two"));
  CAD_EXPECT_EQ(value.GetBool("c"), true);
  CAD_EXPECT(value.Find("d") != nullptr);
  CAD_EXPECT(value.Find("d")->is_null());
  const JsonValue* array = value.GetChildArray("e");
  CAD_EXPECT(array != nullptr);
  CAD_EXPECT_EQ(array->items().size(), size_t(3));
}

CAD_TEST(JsonMissingKeysUseDefaults) {
  const JsonValue value = JsonValue::Parse(R"({"a":1})");
  CAD_EXPECT_EQ(value.GetString("missing", "fallback"), std::string("fallback"));
  CAD_EXPECT_EQ(value.GetInt("missing", 42), int64_t(42));
  CAD_EXPECT_EQ(value.GetBool("missing", true), true);
  // A key of the wrong type also falls back rather than coercing.
  CAD_EXPECT_EQ(value.GetString("a", "fallback"), std::string("fallback"));
}

CAD_TEST(JsonRoundTripsEscapesAndUnicode) {
  // Kept ASCII-only on purpose: the source is compiled without /utf-8, and the
  // point here is that the \u escapes decode, including a surrogate pair.
  const std::string text =
      "{\"s\":\"line\\nbreak \\\"quoted\\\" \\u00e9 \\ud83d\\ude00\"}";
  const JsonValue value = JsonValue::Parse(text);
  const std::string decoded = value.GetString("s");
  CAD_EXPECT(decoded.find('\n') != std::string::npos);
  CAD_EXPECT(decoded.find('"') != std::string::npos);
  // U+00E9 is two UTF-8 bytes, U+1F600 is four.
  CAD_EXPECT(decoded.find("\xC3\xA9") != std::string::npos);
  CAD_EXPECT(decoded.find("\xF0\x9F\x98\x80") != std::string::npos);
  // Round-tripping must preserve the string exactly.
  const JsonValue again = JsonValue::Parse(value.Serialize());
  CAD_EXPECT_EQ(again.GetString("s"), decoded);
}

CAD_TEST(JsonSkipsUtf8ByteOrderMark) {
  // Windows editors add a BOM. A settings file saved from Notepad must still
  // parse, or the user's settings silently revert to defaults.
  std::string error;
  const JsonValue value =
      JsonValue::Parse("\xEF\xBB\xBF{\"a\":1}", &error);
  CAD_EXPECT_EQ(error, std::string());
  CAD_EXPECT(value.is_object());
  CAD_EXPECT_EQ(value.GetInt("a"), int64_t(1));
}

CAD_TEST(SettingsSurviveAByteOrderMark) {
  const JsonValue json = JsonValue::Parse(
      "\xEF\xBB\xBF{\"overlay\":{\"enabled\":true},"
      "\"general\":{\"theme\":\"dark\"}}");
  const Settings s = SettingsStore::FromJson(json);
  CAD_EXPECT_EQ(s.overlay_enabled, true);
  CAD_EXPECT_EQ(s.theme, std::string("dark"));
}

CAD_TEST(JsonRejectsMalformedInput) {
  std::string error;
  CAD_EXPECT(JsonValue::Parse("{", &error).is_null());
  CAD_EXPECT(!error.empty());
  CAD_EXPECT(JsonValue::Parse("{\"a\":}", &error).is_null());
  CAD_EXPECT(JsonValue::Parse("[1,2", &error).is_null());
  CAD_EXPECT(JsonValue::Parse("{\"a\":1}trailing", &error).is_null());
  CAD_EXPECT(JsonValue::Parse("", &error).is_null());
}

CAD_TEST(JsonRejectsDeeplyNestedInput) {
  // A pathological file must fail cleanly rather than exhaust the stack.
  std::string text(200, '[');
  std::string error;
  CAD_EXPECT(JsonValue::Parse(text, &error).is_null());
  CAD_EXPECT(!error.empty());
}

CAD_TEST(JsonWritesIntegersWithoutDecimalPoints) {
  JsonValue object = JsonValue::Object();
  object.Set("count", JsonValue::Int(42));
  object.Set("ratio", JsonValue::Number(0.5));
  const std::string text = object.Serialize();
  CAD_EXPECT(text.find("\"count\":42") != std::string::npos);
  CAD_EXPECT(text.find("42.0") == std::string::npos);
  CAD_EXPECT(text.find("0.5") != std::string::npos);
}

CAD_TEST(JsonPreservesKeyOrder) {
  JsonValue object = JsonValue::Object();
  object.Set("zebra", JsonValue::Int(1));
  object.Set("apple", JsonValue::Int(2));
  const std::string text = object.Serialize();
  CAD_EXPECT(text.find("zebra") < text.find("apple"));
}

CAD_TEST(SettingsRoundTripThroughJson) {
  Settings original;
  original.theme = "dark";
  original.search_url = "https://example.test/?s=%s";
  original.overlay_enabled = true;
  original.overlay_position = "left";
  original.discard_idle_minutes = 45;
  original.allowed_sites = {"onshape.com", "autodesk.com"};
  original.window_width = 1600;

  const Settings restored =
      SettingsStore::FromJson(SettingsStore::ToJson(original));

  CAD_EXPECT_EQ(restored.theme, std::string("dark"));
  CAD_EXPECT_EQ(restored.search_url, std::string("https://example.test/?s=%s"));
  CAD_EXPECT_EQ(restored.overlay_enabled, true);
  CAD_EXPECT_EQ(restored.overlay_position, std::string("left"));
  CAD_EXPECT_EQ(restored.discard_idle_minutes, 45);
  CAD_EXPECT_EQ(restored.allowed_sites.size(), size_t(2));
  CAD_EXPECT_EQ(restored.window_width, 1600);
}

CAD_TEST(SettingsRejectBadValuesAndClampRanges) {
  const JsonValue json = JsonValue::Parse(R"({
    "general": {"theme": "chartreuse", "startup_mode": "nonsense"},
    "performance": {"discard_idle_minutes": 999999, "discard_available_fraction": 9.0},
    "overlay": {"position": "middle", "opacity": 500}
  })");
  const Settings s = SettingsStore::FromJson(json);

  CAD_EXPECT_EQ(s.theme, std::string("system"));
  CAD_EXPECT_EQ(s.startup_mode, std::string("restore"));
  CAD_EXPECT_EQ(s.overlay_position, std::string("bottom"));
  CAD_EXPECT(s.discard_idle_minutes <= 24 * 60);
  CAD_EXPECT(s.discard_available_fraction <= 0.50);
  CAD_EXPECT(s.overlay_opacity <= 100);
}

CAD_TEST(OldCornerOverlayPositionsMigrateToAnEdge) {
  // The overlay used to be a corner panel. A settings file written then must
  // keep the side the user picked rather than silently reverting to default.
  struct Case {
    const char* stored;
    const char* expected;
  };
  const Case cases[] = {
      {"top-left", "top"},     {"top-right", "top"},
      {"bottom-left", "bottom"}, {"bottom-right", "bottom"},
      {"left", "left"},        {"right", "right"},
  };
  for (const Case& test : cases) {
    const std::string text =
        std::string("{\"overlay\":{\"position\":\"") + test.stored + "\"}}";
    const Settings s = SettingsStore::FromJson(JsonValue::Parse(text));
    CAD_EXPECT_EQ(s.overlay_position, std::string(test.expected));
  }
}

CAD_TEST(EmptyJsonGivesDefaultSettings) {
  const Settings s = SettingsStore::FromJson(JsonValue::Parse("{}"));
  const Settings defaults;
  CAD_EXPECT_EQ(s.theme, defaults.theme);
  CAD_EXPECT_EQ(s.search_url, defaults.search_url);
  CAD_EXPECT_EQ(s.blocklist_enabled, defaults.blocklist_enabled);
  CAD_EXPECT_EQ(s.overlay_enabled, false);
}
