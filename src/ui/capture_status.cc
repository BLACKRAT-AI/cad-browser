#include "ui/capture_status.h"

#include <algorithm>

#include "include/views/cef_button_delegate.h"

namespace cad {
namespace {

constexpr int kHeight = 26;
constexpr int kHorizontalPadding = 20;
constexpr int kMargin = 10;
constexpr int kMaxWidth = 640;
constexpr char kAutoCaptureLine[] = "\xE2\x97\x8F Auto-capture on";  // U+25CF

// A label button needs a delegate. This one ignores clicks: the notice is
// there to be read, not pressed.
class InertButtonDelegate : public CefButtonDelegate {
 public:
  InertButtonDelegate() = default;
  void OnButtonPressed(CefRefPtr<CefButton> button) override {}

 private:
  IMPLEMENT_REFCOUNTING(InertButtonDelegate);
};

}  // namespace

void CaptureStatus::Attach(CefRefPtr<CefWindow> window, const Theme& theme) {
  if (controller_ || !window) {
    return;
  }
  window_ = window;
  theme_ = theme;
  label_ = CefLabelButton::CreateLabelButton(new InertButtonDelegate(), "");
  label_->SetInkDropEnabled(false);
  label_->SetFocusable(false);
  label_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  label_->SetFontList(UiFontList(12));
  // can_activate false: the notice never takes focus or a click from the page.
  controller_ = window_->AddOverlayView(label_, CEF_DOCKING_MODE_CUSTOM,
                                        /*can_activate=*/false);
  if (controller_) {
    controller_->SetVisible(false);
  }
  // Colours go on after the overlay exists; set earlier they are dropped.
  ApplyTheme(theme_);
}

void CaptureStatus::Detach() {
  if (controller_) {
    controller_->Destroy();
  }
  controller_ = nullptr;
  label_ = nullptr;
  window_ = nullptr;
}

void CaptureStatus::ApplyTheme(const Theme& theme) {
  theme_ = theme;
  if (!label_) {
    return;
  }
  // Fully opaque: a translucent view does not composite over the page.
  label_->SetBackgroundColor(theme_.chrome_surface);
  label_->SetEnabledTextColors(message_is_error_ && !message_.empty()
                                   ? theme_.danger
                                   : theme_.text);
}

void CaptureStatus::Layout(const CefRect& page_area) {
  page_area_ = page_area;
  if (!controller_ || !label_ || !controller_->IsVisible()) {
    return;
  }
  const CefSize preferred = label_->GetPreferredSize();
  const int width = std::min(preferred.width + kHorizontalPadding,
                             std::max(0, page_area_.width - 2 * kMargin));
  const int capped = std::min(width, kMaxWidth);
  controller_->SetBounds(CefRect(page_area_.x + kMargin,
                                 page_area_.y + page_area_.height - kHeight -
                                     kMargin,
                                 capped, kHeight));
}

void CaptureStatus::ShowMessage(const std::string& message,
                                uint64_t until_ms,
                                bool error) {
  message_ = message;
  message_until_ms_ = until_ms;
  message_is_error_ = error;
  Update(0);
}

void CaptureStatus::SetAutoCapture(bool enabled) {
  auto_capture_ = enabled;
  Update(0);
}

void CaptureStatus::Refresh(uint64_t now_ms) {
  Update(now_ms);
}

void CaptureStatus::Update(uint64_t now_ms) {
  if (now_ms != 0 && now_ms >= message_until_ms_) {
    message_.clear();
  }
  if (!controller_ || !label_) {
    return;
  }
  std::string text = message_;
  if (text.empty() && auto_capture_) {
    text = kAutoCaptureLine;
  }
  if (text.empty()) {
    controller_->SetVisible(false);
    return;
  }
  label_->SetText(text);
  ApplyTheme(theme_);
  controller_->SetVisible(true);
  Layout(page_area_);
}

}  // namespace cad
