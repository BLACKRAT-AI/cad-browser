// CAD Browser - MainWindow's part of page capture: the command, auto-capture
// scheduling, writing the file off the UI thread and the notice.
//
// Kept out of main_window.cc, which is long enough already.
#include <chrono>
#include <ctime>
#include <utility>

#include "app/app_state.h"
#include "browser/page_capture.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "ui/main_window.h"

namespace cad {
namespace {

constexpr uint64_t kCaptureMessageMs = 4000;

uint64_t CaptureNowMs() {
  using clock = std::chrono::steady_clock;
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          clock::now().time_since_epoch())
          .count());
}

CaptureTime ToCaptureTime(const std::tm& tm) {
  CaptureTime t;
  t.year = tm.tm_year + 1900;
  t.month = tm.tm_mon + 1;
  t.day = tm.tm_mday;
  t.hour = tm.tm_hour;
  t.minute = tm.tm_min;
  t.second = tm.tm_sec;
  return t;
}

}  // namespace

void MainWindow::CaptureActivePage() {
  Tab* tab = tab_manager_ ? tab_manager_->active_tab() : nullptr;
  if (!tab || !IsCapturableUrl(tab->url())) {
    ShowCaptureMessage("Nothing to capture on this page", true);
    return;
  }
  if (!tab->RequestCapture(/*automatic=*/false)) {
    ShowCaptureMessage("This tab is asleep; open it and try again", true);
  }
}

void MainWindow::OnAutoCaptureChanged(bool enabled) {
  capture_status_.ApplyTheme(theme_);
  capture_status_.SetAutoCapture(enabled);
  auto_capture_.clear();
  // Turning it on also saves the page already in front of the user, since
  // that is usually the one they turned it on for.
  if (enabled && tab_manager_) {
    OnTabPageSettled(tab_manager_->active_tab_id());
  }
}

void MainWindow::OnTabPageSettled(int tab_id) {
  AppState* state = AppState::Get();
  if (closing_ || !state || !state->auto_capture() || tab_id <= 0) {
    return;
  }
  const uint64_t generation = ++auto_capture_[tab_id].generation;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(
          [](CefRefPtr<MainWindow> self, int id, uint64_t expected) {
            AppState* app = AppState::Get();
            if (self->closing_ || !self->tab_manager_ || !app ||
                !app->auto_capture()) {
              return;
            }
            auto it = self->auto_capture_.find(id);
            // A later event for this tab restarted the wait.
            if (it == self->auto_capture_.end() ||
                it->second.generation != expected) {
              return;
            }
            Tab* tab = self->tab_manager_->FindTab(id);
            if (!tab || tab->is_loading() || tab->is_discarded() ||
                !IsCapturableUrl(tab->url())) {
              return;
            }
            AutoCaptureState& seen = it->second;
            if (seen.last_url == tab->url() &&
                seen.last_load_count == tab->load_count()) {
              return;  // already saved this page
            }
            if (tab->RequestCapture(/*automatic=*/true)) {
              seen.last_url = tab->url();
              seen.last_load_count = tab->load_count();
            }
          },
          CefRefPtr<MainWindow>(this), tab_id, generation),
      kAutoCaptureDebounceMs);
}

void MainWindow::OnPageCaptured(Tab* tab,
                                bool automatic,
                                bool ok,
                                const std::string& error,
                                PageCapture page) {
  if (closing_) {
    return;
  }
  if (!ok) {
    ShowCaptureMessage("Capture failed: " + error, true);
    return;
  }
  AppState* state = AppState::Get();
  const std::string folder =
      state ? state->settings().capture_folder : std::string(kDefaultCaptureFolder);

  const std::time_t now = std::time(nullptr);
  std::tm local_tm{};
  std::tm utc_tm{};
  localtime_s(&local_tm, &now);
  gmtime_s(&utc_tm, &now);
  const std::string name =
      CaptureFileName(ToCaptureTime(local_tm), page.url, page.title);
  std::string json =
      CaptureToJson(page, CaptureTimestampUtc(ToCaptureTime(utc_tm))) + "\n";

  // A big page is several megabytes; write it on a file thread, not the UI.
  CefPostTask(
      TID_FILE_USER_BLOCKING,
      base::BindOnce(
          [](CefRefPtr<MainWindow> self, std::string dir, std::string file,
             std::string contents) {
            const std::string written = SaveCaptureFile(dir, file, contents);
            CefPostTask(TID_UI,
                        base::BindOnce(
                            [](CefRefPtr<MainWindow> window, std::string dir,
                               std::string written) {
                              if (written.empty()) {
                                window->ShowCaptureMessage(
                                    "Could not save to " + dir, true);
                              } else {
                                window->ShowCaptureMessage("Saved " + written,
                                                           false);
                              }
                            },
                            self, dir, written));
          },
          CefRefPtr<MainWindow>(this), folder, name, std::move(json)));
}

void MainWindow::ShowCaptureMessage(const std::string& message, bool error) {
  if (closing_) {
    return;
  }
  capture_status_.ApplyTheme(theme_);
  const uint64_t until = CaptureNowMs() + kCaptureMessageMs;
  capture_status_.ShowMessage(message, until, error);
  LayoutCaptureStatus();
  const uint64_t generation = ++capture_message_generation_;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(
          [](CefRefPtr<MainWindow> self, uint64_t expected) {
            if (!self->closing_ &&
                self->capture_message_generation_ == expected) {
              self->capture_status_.Refresh(CaptureNowMs());
            }
          },
          CefRefPtr<MainWindow>(this), generation),
      static_cast<int64_t>(kCaptureMessageMs + 100));
}

void MainWindow::LayoutCaptureStatus() {
  if (!window_) {
    return;
  }
  // Overlay bounds are in the window's client coordinates. The page panel
  // excludes the stats bar, so the notice never sits on top of it.
  const CefRect client = window_->GetClientAreaBoundsInScreen();
  CefRect area(0, 0, client.width, client.height);
  if (content_panel_) {
    const CefRect panel = content_panel_->GetBoundsInScreen();
    if (panel.width > 0 && panel.height > 0) {
      area = CefRect(panel.x - client.x, panel.y - client.y, panel.width,
                     panel.height);
    }
  }
  capture_status_.Layout(area);
}

}  // namespace cad
