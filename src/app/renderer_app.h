// CAD Browser - the renderer-process side.
//
// Two jobs, both deliberately small (PLAN.md section 8.1):
//
//   1. Frame timing. A requestAnimationFrame loop measures real frame
//      intervals and reports them to the browser process four times a second.
//      It never touches the page's DOM and leaves no global behind: the
//      callback is handed to a closure and the property that carried it is
//      deleted immediately. When the overlay is off the loop is not started
//      at all, and an already-running loop stops at its next frame, so the
//      cost with the overlay off is exactly zero.
//
//   2. The cad:// bridge. Internal pages get window.cadBrowser.send(name,
//      payload) so settings and the system page can talk to the browser
//      process. It is installed only for the cad:// scheme; ordinary web
//      pages never see it.
#pragma once

#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"

namespace cad {

class RendererApp : public CefRenderProcessHandler {
 public:
  RendererApp();

  bool probe_enabled() const { return probe_enabled_; }

  // CefRenderProcessHandler
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  void InstallFrameProbe(CefRefPtr<CefV8Context> context);
  // onshape.com only. Installs the focus-mode controller and sets it to the
  // current state; the script itself re-checks the host and does nothing
  // anywhere else.
  void InstallSiteFocus(CefRefPtr<CefV8Context> context);
  void InstallCadBridge(CefRefPtr<CefV8Context> context);

  bool probe_enabled_ = false;
  // Read once from the command line, because a renderer can be spawned at any
  // time and has to start in the right state without asking anyone.
  bool site_focus_enabled_ = false;
  bool site_focus_read_ = false;

  IMPLEMENT_REFCOUNTING(RendererApp);
  DISALLOW_COPY_AND_ASSIGN(RendererApp);
};

}  // namespace cad
