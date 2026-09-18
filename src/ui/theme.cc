#include "sys/win_util.h"

#include "ui/theme.h"

#include <cstdio>

namespace cad {
namespace {

constexpr cef_color_t Rgb(uint32_t rgb) {
  return 0xFF000000u | rgb;
}

constexpr cef_color_t Argb(uint32_t alpha, uint32_t rgb) {
  return (alpha << 24) | rgb;
}

}  // namespace

bool WindowsPrefersDarkMode() {
  // AppsUseLightTheme is 0 when the user has chosen dark mode for apps.
  const auto value = sys::RegReadDword(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme");
  if (!value.has_value())
    return false;
  return *value == 0;
}

std::string Theme::ToCss(cef_color_t color) const {
  char buffer[16];
  ::_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "#%02X%02X%02X",
                (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
  return buffer;
}

Theme BuildTheme(const std::string& preference) {
  bool dark;
  if (preference == "dark") {
    dark = true;
  } else if (preference == "light") {
    dark = false;
  } else {
    dark = WindowsPrefersDarkMode();
  }

  Theme theme;
  theme.dark = dark;
  // One accent in both themes: a UI that changes hue between modes stops
  // reading as the same product.
  theme.accent = Rgb(0x2F6FED);
  theme.danger = Rgb(0xE5484D);
  theme.warning = Rgb(0xF2B33D);

  if (dark) {
    theme.background = Rgb(0x1B1D1F);
    theme.surface = Rgb(0x242628);
    theme.surface_hover = Rgb(0x2E3134);
    theme.border = Rgb(0x35393D);
    theme.text = Rgb(0xE6E8EA);
    theme.text_muted = Rgb(0x9AA0A6);
    theme.chrome_surface = Argb(0xFF, 0x202325);
    theme.chrome_hover = Argb(0xFF, 0x30353A);
    theme.chrome_pressed = Argb(0xFF, 0x3C4248);
    theme.tab_active = Argb(0xFF, 0x303438);
    theme.tab_inactive = Argb(0x00, 0x000000);
    theme.toolbar = Argb(0xFF, 0x202325);
    theme.omnibox = Rgb(0x2A2D30);
  } else {
    theme.background = Rgb(0xF5F6F7);
    theme.surface = Rgb(0xFFFFFF);
    theme.surface_hover = Rgb(0xECEEF0);
    theme.border = Rgb(0xD9DCE0);
    theme.text = Rgb(0x1B1D1F);
    theme.text_muted = Rgb(0x6B7178);
    theme.chrome_surface = Argb(0xFF, 0x23262A);
    theme.chrome_hover = Argb(0xFF, 0x33383D);
    theme.chrome_pressed = Argb(0xFF, 0x3F454B);
    theme.tab_active = Argb(0xFF, 0x33373C);
    theme.tab_inactive = Argb(0x00, 0x000000);
    theme.toolbar = Argb(0xFF, 0x23262A);
    theme.omnibox = Rgb(0xFFFFFF);
  }

  // The overlay is always dark so it stays legible over a CAD viewport, which
  // is usually a mid-grey gradient whatever the app theme is.
  theme.overlay_background = Argb(0xB8, 0x141618);
  return theme;
}

std::string UiFontList(int size_px, bool semibold) {
  // CEF font lists are "family[, family...], [style ]size px".
  const char* weight = semibold ? "Semibold " : "";
  char buffer[96];
  ::_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "Segoe UI Variable Text, Segoe UI, %s%dpx", weight, size_px);
  return buffer;
}

std::string MonoFontList(int size_px) {
  char buffer[96];
  ::_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "Cascadia Mono, Consolas, %dpx", size_px);
  return buffer;
}

std::string IconFontList(int size_px) {
  // Segoe Fluent Icons ships with Windows 11, Segoe MDL2 Assets with Windows
  // 10. Listing both means the glyphs are right on either.
  char buffer[96];
  ::_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "Segoe Fluent Icons, Segoe MDL2 Assets, %dpx", size_px);
  return buffer;
}

}  // namespace cad
