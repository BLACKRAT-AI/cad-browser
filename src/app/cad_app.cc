#include "app/cad_app.h"

#include "app/app_state.h"
#include "include/cef_command_line.h"
#include "browser/scheme_handler.h"
#include "browser/session.h"
#include "ui/main_window.h"

namespace cad {

CadApp::CadApp() : renderer_app_(new RendererApp()) {}

void CadApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  // An empty process type means the browser process. Child processes receive
  // the switches on the command line the browser built for them, so applying
  // them again there would at best be a no-op and at worst diverge.
  if (!process_type.empty()) {
    return;
  }
  ApplyChromiumSwitches(command_line, options_);
}

void CadApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  // Called in every process; all of them must agree on what cad:// is.
  RegisterCadCustomScheme(registrar);
}

void CadApp::OnBeforeChildProcessLaunch(
    CefRefPtr<CefCommandLine> command_line) {
  // Called on the browser process IO thread. options_ is written once before
  // CefInitialize and never again, so reading it here needs no lock. A change
  // made from the menu reaches renderers over cad.sitefocus instead.
  if (command_line && options_.onshape_focus &&
      !command_line->HasSwitch("cad-onshape-focus")) {
    command_line->AppendSwitch("cad-onshape-focus");
  }
}

void CadApp::OnContextInitialized() {
  RegisterCadSchemeHandlerFactory();

  AppState* state = AppState::Get();
  if (!state) {
    return;
  }
  // Before anything decides which window to open. This once sat below the
  // command-line URL handling, which returns early, so launching with a URL
  // skipped it entirely: no stats sampler, no blocklist and no GPU
  // preference. The stats bar sat on "collecting..." forever and it looked
  // like an engine bug.
  state->Initialize();

  // URLs on the command line win over the startup setting. A browser that
  // ignores the address it was asked to open is broken, and this one did:
  // with startup set to "restore" the argument was dropped and the previous
  // session came back instead. The saved session is not lost, only not
  // restored, because it is written again when this window closes.
  if (CefRefPtr<CefCommandLine> command_line =
          CefCommandLine::GetGlobalCommandLine()) {
    CefCommandLine::ArgumentList arguments;
    command_line->GetArguments(arguments);
    SessionWindow requested;
    for (const CefString& argument : arguments) {
      const std::string url = argument.ToString();
      if (url.empty() || url[0] == '-') {
        continue;
      }
      SessionTab tab;
      tab.url = url;
      requested.tabs.push_back(std::move(tab));
    }
    if (!requested.tabs.empty()) {
      requested.active_index = 0;
      MainWindow::CreateNewWindow(&requested);
      return;
    }
  }

  // Startup behaviour, per Settings > General.
  const std::string mode = state->settings().startup_mode;
  if (mode == "restore") {
    const Session session = LoadSession(state->SessionPath());
    if (!session.empty()) {
      for (const SessionWindow& window : session.windows) {
        MainWindow::CreateNewWindow(&window);
      }
      return;
    }
  }

  if (mode == "homepage" && !state->settings().homepage.empty()) {
    SessionWindow window;
    SessionTab tab;
    tab.url = state->settings().homepage;
    window.tabs.push_back(std::move(tab));
    window.active_index = 0;
    MainWindow::CreateNewWindow(&window);
    return;
  }

  MainWindow::CreateNewWindow(nullptr);
}

}  // namespace cad
