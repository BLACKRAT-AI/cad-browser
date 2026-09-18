// CAD Browser - colour tokens and metrics for the native chrome.
//
// PLAN.md section 9. Flat, one accent, light and dark following Windows. Every
// colour the UI uses is named here; nothing else in ui/ writes a literal.
#pragma once

#include <string>

#include "include/internal/cef_types.h"

namespace cad {

struct Theme {
  bool dark = false;

  cef_color_t background = 0;
  cef_color_t surface = 0;
  cef_color_t surface_hover = 0;
  cef_color_t border = 0;
  cef_color_t text = 0;
  cef_color_t text_muted = 0;
  cef_color_t accent = 0;
  cef_color_t danger = 0;
  cef_color_t warning = 0;
  cef_color_t overlay_background = 0;
  cef_color_t tab_active = 0;
  cef_color_t tab_inactive = 0;
  // Background of the floating chrome. Fully opaque, and it has to be: a
  // translucent colour on an overlay view does not composite against the page
  // underneath, it composites against the overlay's own empty layer, which
  // came out white and made half the bar look unstyled.
  cef_color_t chrome_surface = 0;
  // Hover and pressed fills for buttons in the chrome. Views ink drops are
  // off here, so without these a button gives no response to the pointer at
  // all and feels dead.
  cef_color_t chrome_hover = 0;
  cef_color_t chrome_pressed = 0;
  cef_color_t toolbar = 0;
  cef_color_t omnibox = 0;

  // "#1B1D1F", for handing a colour to a cad:// page.
  std::string ToCss(cef_color_t color) const;
};

// Layout constants, in density-independent pixels.
namespace metrics {
inline constexpr int kTabStripHeight = 32;
inline constexpr int kToolbarHeight = 38;

// The chrome floats above the page rather than taking layout space from it, so
// the page is full height at all times and revealing the chrome never reflows
// it. A CAD viewport that relaid out every time the pointer passed the top of
// the window would be unusable.
inline constexpr int kChromeHeight = kTabStripHeight + kToolbarHeight;

// Pointer within this many pixels of the top of the page area reveals the
// chrome. Five pixels was the first attempt and it was unusable: a pointer
// thrown at the top of the screen crosses that band in one frame, and on a
// maximised window the band was partly off-screen. This is measured against
// the client area, not the window rect, which is what made the maximised case
// wrong.
inline constexpr int kChromeRevealZone = 14;

// Width of the strip at the top centre that does not summon the chrome,
// reserved for a page-level control that sits there. Slightly wider than the
// control itself so the pointer can approach it without the bar appearing.
inline constexpr int kPageToggleGuardWidth = 96;

// Once revealed, the chrome stays until the pointer is this far below it.
// Without the gap it flickers when the pointer sits near the boundary.
inline constexpr int kChromeHideMargin = 24;

// How often the pointer is checked, in milliseconds. Views gives a client no
// window-wide mouse-move callback, so this polls GetCursorPos.
inline constexpr int kChromeHoverPollMs = 40;
inline constexpr int kTabMaxWidth = 240;
inline constexpr int kTabMinWidth = 40;
inline constexpr int kTabPinnedWidth = 44;
inline constexpr int kToolbarButtonWidth = 38;
inline constexpr int kOmniboxHeight = 30;
inline constexpr int kWindowPadding = 6;
// The stats bar takes its own space in the layout rather than floating over
// the page. Covering the thing being measured is not a reasonable thing for a
// measurement tool to do.
inline constexpr int kOverlayBarHeight = 24;
// Docked left or right it is a full-height rail, kept as narrow as the
// numbers allow.
inline constexpr int kOverlayRailWidth = 150;
}  // namespace metrics

// "system" | "light" | "dark". "system" follows the Windows apps theme.
Theme BuildTheme(const std::string& preference);

// True when Windows is set to dark mode for applications.
bool WindowsPrefersDarkMode();

// The font list CEF Views wants, e.g. "Segoe UI, 12px".
std::string UiFontList(int size_px, bool semibold = false);
std::string MonoFontList(int size_px);
// Segoe Fluent Icons / Segoe MDL2 Assets, the fonts Windows draws its own
// chrome with. Using them is what makes the toolbar and caption buttons look
// native rather than like typed characters.
std::string IconFontList(int size_px);

// Icon glyphs, written as UTF-8 escapes so the source stays ASCII: the build
// does not pass /utf-8, and a literal glyph here would depend on the code page
// the file happens to be saved in.
//
// These are ordinary Unicode symbols rather than the private-use codepoints of
// Segoe Fluent Icons. The icon fonts are installed, but Views rendered the
// private-use range as missing-glyph boxes, and chrome that depends on one
// specific font resolving is chrome that breaks on someone else's machine.
// Every glyph below is present in the standard Windows UI fonts.
namespace icons {
inline constexpr char kBack[] = "\xE2\x86\x90";      // U+2190 left arrow
inline constexpr char kForward[] = "\xE2\x86\x92";   // U+2192 right arrow
// U+21BB draws its gap and arrowhead at the top, which reads as a lopsided
// half circle at this size. U+27F3 is the same clockwise reload arrow with the
// gap turned round to the right, which is what it looks like rotated a quarter
// turn and is the glyph other browsers use. A native Views button draws text
// rather than an image, so the icon cannot be rotated; the only lever is which
// codepoint is asked for.
inline constexpr char kReload[] = "\xE2\x9F\xB3";    // U+27F3 gapped circle arrow
inline constexpr char kStop[] = "\xC3\x97";          // U+00D7 multiplication
inline constexpr char kMenu[] = "\xE2\x89\xA1";      // U+2261 three bars
inline constexpr char kAdd[] = "+";
inline constexpr char kClose[] = "\xC3\x97";         // U+00D7
inline constexpr char kMinimize[] = "\xE2\x80\x93";  // U+2013 en dash
inline constexpr char kMaximize[] = "\xE2\x96\xA1";  // U+25A1 white square
inline constexpr char kRestore[] = "\xE2\x96\xA3";   // U+25A3 square in square
inline constexpr char kSleeping[] = "\xE2\x97\x8B";  // U+25CB white circle
}  // namespace icons

}  // namespace cad
