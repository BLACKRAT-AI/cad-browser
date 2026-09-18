// CAD Browser - the stats bar.
//
// A small CefBrowserView showing cad://overlay. It is a **sibling of the page
// in the window layout**, not something floating on top of it: a panel that
// covers the viewport it is reporting on is worse than no panel. MainWindow
// places it, and the page shrinks by exactly its thickness.
//
// Docked top or bottom it is a single-line bar across the full width. Docked
// left or right it is a narrow full-height rail, because a line of text
// cannot be read sideways.
//
// It is moved by dragging it to an edge, or by double-clicking to cycle.
#pragma once

#include <string>

#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "sys/stats.h"

namespace cad {

struct Settings;
class ClientHandler;

// Which edge of the page area the bar occupies.
enum class OverlayEdge { kTop, kBottom, kLeft, kRight };

OverlayEdge OverlayEdgeFromString(const std::string& value);
const char* OverlayEdgeToString(OverlayEdge edge);
bool OverlayEdgeIsVertical(OverlayEdge edge);

class OverlayView {
 public:
  class Delegate {
   public:
    // The user dragged the bar to a different edge. The window has to redo its
    // layout and persist the choice.
    virtual void OnOverlayEdgeChanged(OverlayEdge edge) = 0;
    // A click landed on the bar; hand focus back to the page.
    virtual void OnOverlayDragFinished() = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit OverlayView(Delegate* delegate);
  ~OverlayView();
  OverlayView(const OverlayView&) = delete;
  OverlayView& operator=(const OverlayView&) = delete;

  // Creates the browser view. MainWindow adds it to its layout.
  void Create(const Settings& settings);
  void Destroy();

  CefRefPtr<CefBrowserView> view() const { return browser_view_; }
  OverlayEdge edge() const { return edge_; }

  void SetVisible(bool visible);
  bool visible() const { return visible_; }

  // Pushes a new sample into the page.
  void Update(const sys::StatsSnapshot& snapshot, const Settings& settings);

  // Re-reads the edge from settings. Returns true when it changed, which
  // means the window layout has to be rebuilt.
  bool ApplySettings(const Settings& settings);

  // Called by the bar's client when the page reports a new edge.
  void OnEdgeReportedByPage(const std::string& edge);

 private:
  void ApplyPreferredSize();

  Delegate* const delegate_;
  CefRefPtr<CefBrowserView> browser_view_;
  CefRefPtr<CefBrowserViewDelegate> view_delegate_;
  CefRefPtr<ClientHandler> client_;
  // Owns the ClientHandler::Delegate that routes the page's messages here.
  class PageBridge;
  PageBridge* bridge_ = nullptr;
  OverlayEdge edge_ = OverlayEdge::kBottom;
  bool visible_ = false;
};

}  // namespace cad
