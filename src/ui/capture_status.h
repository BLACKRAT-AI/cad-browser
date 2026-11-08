// CAD Browser - the one-line notice for page capture.
//
// A small native overlay in the bottom-left corner of the page area. It says
// "Saved <file>" for a few seconds after each capture, and while auto-capture
// is on it stays up with "Auto-capture on" so the user always knows the
// browser is writing down what they look at.
//
// It is a native view, not page content: nothing is added to the page.
#pragma once

#include <cstdint>
#include <string>

#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_window.h"
#include "ui/theme.h"

namespace cad {

class CaptureStatus {
 public:
  CaptureStatus() = default;
  CaptureStatus(const CaptureStatus&) = delete;
  CaptureStatus& operator=(const CaptureStatus&) = delete;

  // Creates the overlay on `window`. Safe to call more than once.
  void Attach(CefRefPtr<CefWindow> window, const Theme& theme);
  void Detach();
  void ApplyTheme(const Theme& theme);

  // `page_area` is the page's rectangle in window client coordinates.
  void Layout(const CefRect& page_area);

  // Shows `message` until `until_ms` (monotonic milliseconds), then falls back
  // to the auto-capture line or hides.
  void ShowMessage(const std::string& message, uint64_t until_ms, bool error);
  void SetAutoCapture(bool enabled);
  // Called when a message may have expired.
  void Refresh(uint64_t now_ms);

 private:
  void Update(uint64_t now_ms);

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefLabelButton> label_;
  CefRefPtr<CefOverlayController> controller_;
  Theme theme_;
  CefRect page_area_;
  std::string message_;
  uint64_t message_until_ms_ = 0;
  bool message_is_error_ = false;
  bool auto_capture_ = false;
};

}  // namespace cad
