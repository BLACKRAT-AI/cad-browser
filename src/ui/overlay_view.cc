#include "ui/overlay_view.h"

#include "browser/client_handler.h"
#include "browser/json.h"
#include "browser/settings_store.h"
#include "include/cef_browser.h"
#include "sys/win_util.h"
#include "ui/theme.h"

namespace cad {
namespace {

// Alloy style, no toolbar, and a preferred size that decides how much of the
// window the bar takes. The box layout stretches the other axis, so only the
// thickness matters: height when docked top or bottom, width when docked left
// or right.
class BarViewDelegate : public CefBrowserViewDelegate {
 public:
  explicit BarViewDelegate(const CefSize& size) : size_(size) {}

  void set_size(const CefSize& size) { size_ = size; }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override { return size_; }
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override { return size_; }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  ChromeToolbarType GetChromeToolbarType(
      CefRefPtr<CefBrowserView> browser_view) override {
    return CEF_CTT_NONE;
  }

 private:
  CefSize size_;

  IMPLEMENT_REFCOUNTING(BarViewDelegate);
  DISALLOW_COPY_AND_ASSIGN(BarViewDelegate);
};

std::string FormatBytesOrDash(uint64_t bytes) {
  return bytes == 0 ? std::string("-") : sys::FormatBytes(bytes, 1);
}

}  // namespace

OverlayEdge OverlayEdgeFromString(const std::string& value) {
  if (value == "top") {
    return OverlayEdge::kTop;
  }
  if (value == "left") {
    return OverlayEdge::kLeft;
  }
  if (value == "right") {
    return OverlayEdge::kRight;
  }
  if (value == "bottom") {
    return OverlayEdge::kBottom;
  }
  // Settings written before the bar replaced the corner panel used corner
  // names. Map them onto the nearest edge rather than discarding the choice.
  if (value == "top-left" || value == "top-right") {
    return OverlayEdge::kTop;
  }
  return OverlayEdge::kBottom;
}

const char* OverlayEdgeToString(OverlayEdge edge) {
  switch (edge) {
    case OverlayEdge::kTop: return "top";
    case OverlayEdge::kLeft: return "left";
    case OverlayEdge::kRight: return "right";
    case OverlayEdge::kBottom:
    default: return "bottom";
  }
}

bool OverlayEdgeIsVertical(OverlayEdge edge) {
  return edge == OverlayEdge::kLeft || edge == OverlayEdge::kRight;
}

// Routes the bar page's messages back to the OverlayView. Every other
// ClientHandler callback is irrelevant for a page that shows five numbers.
class OverlayView::PageBridge : public ClientHandler::Delegate {
 public:
  explicit PageBridge(OverlayView* owner) : owner_(owner) {}

  void Detach() { owner_ = nullptr; }

  void OnUiMessage(const std::string& name,
                   const std::string& payload) override {
    if (!owner_) {
      return;
    }
    if (name == "overlay.edge") {
      const JsonValue json = JsonValue::Parse(payload);
      owner_->OnEdgeReportedByPage(json.GetString("edge"));
    }
  }

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override {}
  void OnBrowserClosing(CefRefPtr<CefBrowser> browser) override {}
  void OnTitleChanged(const std::string& title) override {}
  void OnUrlChanged(const std::string& url) override {}
  void OnFaviconUrlsChanged(const std::vector<std::string>& urls) override {}
  void OnLoadingStateChanged(bool, bool, bool) override {}
  void OnLoadProgress(double progress) override {}
  void OnStatusMessage(const std::string& message) override {}
  void OnFullscreenChanged(bool fullscreen) override {}
  void OnOpenUrlInNewTab(const std::string& url, bool foreground) override {}
  void OnFrameStats(uint32_t, double, double, double, double) override {}
  void OnDownloadStarted(const std::string& file_name) override {}
  void OnDownloadFinished(const std::string& file_name, bool ok) override {}
  void OnRenderProcessGone(const std::string& reason, bool out_of_memory)
      override {}

 private:
  OverlayView* owner_;
};

OverlayView::OverlayView(Delegate* delegate) : delegate_(delegate) {}

OverlayView::~OverlayView() {
  Destroy();
}

void OverlayView::ApplyPreferredSize() {
  if (!view_delegate_) {
    return;
  }
  const CefSize size =
      OverlayEdgeIsVertical(edge_)
          ? CefSize(metrics::kOverlayRailWidth, 120)
          : CefSize(200, metrics::kOverlayBarHeight);
  static_cast<BarViewDelegate*>(view_delegate_.get())->set_size(size);
  if (browser_view_) {
    browser_view_->InvalidateLayout();
  }
}

void OverlayView::Create(const Settings& settings) {
  if (browser_view_) {
    return;
  }
  edge_ = OverlayEdgeFromString(settings.overlay_position);

  CefBrowserSettings browser_settings;
  // Opaque. A windowed browser view paints its background before and around
  // the page, and a non-opaque value there is treated as unset and falls back
  // to white.
  browser_settings.background_color = 0xFF141618u;

  bridge_ = new PageBridge(this);
  client_ = new ClientHandler(bridge_, /*blocklist=*/nullptr);

  view_delegate_ = new BarViewDelegate(CefSize(200, metrics::kOverlayBarHeight));
  browser_view_ = CefBrowserView::CreateBrowserView(
      client_, "cad://overlay", browser_settings,
      /*extra_info=*/nullptr, /*request_context=*/nullptr, view_delegate_);
  if (!browser_view_) {
    return;
  }
  // The bar accepts a drag but must never become what the keyboard talks to.
  browser_view_->SetFocusable(false);
  ApplyPreferredSize();
  browser_view_->SetVisible(false);
  visible_ = false;
}

void OverlayView::Destroy() {
  if (bridge_) {
    bridge_->Detach();
  }
  if (client_) {
    client_->DetachDelegate();
    client_ = nullptr;
  }
  browser_view_ = nullptr;
  view_delegate_ = nullptr;
  visible_ = false;
  delete bridge_;
  bridge_ = nullptr;
}

void OverlayView::SetVisible(bool visible) {
  visible_ = visible;
  if (browser_view_) {
    browser_view_->SetVisible(visible);
  }
}

bool OverlayView::ApplySettings(const Settings& settings) {
  const OverlayEdge requested = OverlayEdgeFromString(settings.overlay_position);
  if (requested == edge_) {
    return false;
  }
  edge_ = requested;
  ApplyPreferredSize();
  return true;
}

void OverlayView::OnEdgeReportedByPage(const std::string& edge) {
  const OverlayEdge requested = OverlayEdgeFromString(edge);
  if (requested != edge_) {
    edge_ = requested;
    ApplyPreferredSize();
    if (delegate_) {
      delegate_->OnOverlayEdgeChanged(edge_);
    }
  }
  if (delegate_) {
    delegate_->OnOverlayDragFinished();
  }
}

void OverlayView::Update(const sys::StatsSnapshot& snapshot,
                         const Settings& settings) {
  if (!visible_ || !browser_view_) {
    return;
  }
  CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
  if (!browser) {
    return;
  }
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) {
    return;
  }

  JsonValue root = JsonValue::Object();
  root.Set("edge", JsonValue::String(OverlayEdgeToString(edge_)));

  // The bar needs its own position on screen to work out which edge a drag
  // ended nearest to; a pointer event only gives it screen coordinates.
  const CefRect bounds = browser_view_->GetBoundsInScreen();
  JsonValue area = JsonValue::Object();
  area.Set("x", JsonValue::Int(bounds.x));
  area.Set("y", JsonValue::Int(bounds.y));
  area.Set("w", JsonValue::Int(bounds.width));
  area.Set("h", JsonValue::Int(bounds.height));
  root.Set("self", std::move(area));

  JsonValue frame_stats = JsonValue::Object();
  frame_stats.Set("valid", JsonValue::Bool(snapshot.frame.valid));
  frame_stats.Set("fps", JsonValue::Number(snapshot.frame.fps));
  frame_stats.Set("ms", JsonValue::Number(snapshot.frame.frame_ms_p50));
  frame_stats.Set("p99", JsonValue::Number(snapshot.frame.frame_ms_p99));
  root.Set("frame", std::move(frame_stats));

  JsonValue gpu = JsonValue::Object();
  gpu.Set("valid", JsonValue::Bool(snapshot.gpu.valid));
  gpu.Set("name", JsonValue::String(snapshot.gpu.adapter_name));
  gpu.Set("counters", JsonValue::Bool(snapshot.gpu.counters_available));
  gpu.Set("percent", JsonValue::Number(snapshot.gpu.system_percent));
  gpu.Set("vramUsed",
          JsonValue::String(FormatBytesOrDash(snapshot.gpu.vram_used_bytes)));
  gpu.Set("vramTotal",
          JsonValue::String(FormatBytesOrDash(snapshot.gpu.vram_total_bytes)));
  gpu.Set("mismatch", JsonValue::String(snapshot.gpu.mismatch_note));
  root.Set("gpu", std::move(gpu));

  JsonValue memory = JsonValue::Object();
  memory.Set("tab", JsonValue::String(
                        FormatBytesOrDash(snapshot.memory.active_tab_bytes)));
  memory.Set("browser", JsonValue::String(FormatBytesOrDash(
                            snapshot.memory.browser_total_bytes)));
  memory.Set("systemUsed", JsonValue::String(FormatBytesOrDash(
                               snapshot.memory.system_used_bytes)));
  memory.Set("systemTotal", JsonValue::String(FormatBytesOrDash(
                                snapshot.memory.system_usable_bytes)));
  root.Set("memory", std::move(memory));

  JsonValue cpu = JsonValue::Object();
  cpu.Set("valid", JsonValue::Bool(snapshot.cpu.valid));
  cpu.Set("system", JsonValue::Number(snapshot.cpu.system_percent));
  cpu.Set("browser", JsonValue::Number(snapshot.cpu.browser_percent));
  root.Set("cpu", std::move(cpu));

  JsonValue tabs = JsonValue::Object();
  tabs.Set("total", JsonValue::Int(snapshot.tabs.total));
  tabs.Set("sleeping", JsonValue::Int(snapshot.tabs.sleeping));
  tabs.Set("blocked",
           JsonValue::Int(static_cast<int64_t>(snapshot.tabs.blocked_requests)));
  root.Set("tabs", std::move(tabs));

  JsonValue show = JsonValue::Object();
  show.Set("fps", JsonValue::Bool(settings.overlay_show_fps));
  show.Set("gpu", JsonValue::Bool(settings.overlay_show_gpu));
  show.Set("memory", JsonValue::Bool(settings.overlay_show_memory));
  show.Set("cpu", JsonValue::Bool(settings.overlay_show_cpu));
  show.Set("tabs", JsonValue::Bool(settings.overlay_show_tabs));
  root.Set("show", std::move(show));

  root.Set("opacity", JsonValue::Number(settings.overlay_opacity / 100.0));

  const std::string script =
      "window.cadOverlay&&window.cadOverlay.update(" + root.Serialize() + ")";
  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

}  // namespace cad
