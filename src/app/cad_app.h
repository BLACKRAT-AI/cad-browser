// CAD Browser - the CefApp, used by every process.
//
// The same DLL runs as the browser, the renderers, the GPU process and the
// utilities, so this class carries the handlers for all of them and CEF asks
// for the one that applies.
#pragma once

#include "app/renderer_app.h"
#include "app/switches.h"
#include "include/cef_app.h"

namespace cad {

class CadApp : public CefApp, public CefBrowserProcessHandler {
 public:
  CadApp();

  // Set in the browser process only, after settings have been read and before
  // CefInitialize, which is when OnBeforeCommandLineProcessing runs for it.
  void set_switch_options(const SwitchOptions& options) { options_ = options; }

  // ---- CefApp ----
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return renderer_app_;
  }

  // ---- CefBrowserProcessHandler ----
  void OnContextInitialized() override;
  // Chromium copies only the switches it knows about into a child process's
  // command line, so one of ours has to be put back here. Without this the
  // renderers never saw --cad-onshape-focus at all.
  void OnBeforeChildProcessLaunch(
      CefRefPtr<CefCommandLine> command_line) override;

 private:
  SwitchOptions options_;
  CefRefPtr<RendererApp> renderer_app_;

  IMPLEMENT_REFCOUNTING(CadApp);
  DISALLOW_COPY_AND_ASSIGN(CadApp);
};

}  // namespace cad
