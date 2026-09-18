// CAD Browser - process entry point.
//
// CEF 144 provides the Windows sandbox through bootstrap.exe: CADBrowser.exe
// is a renamed copy of it, and it loads CADBrowser.dll and calls the exported
// RunWinMain below with a ready-made sandbox_info. That is how the browser
// keeps a full sandbox without linking cef_sandbox.lib, which would tie the
// build to one exact MSVC toolset.

#include "sys/win_util.h"

#include "app/app_state.h"
#include "app/cad_app.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"

namespace {

int RunMain(HINSTANCE instance,
            LPWSTR command_line,
            int show_command,
            void* sandbox_info) {
  CefMainArgs main_args(instance);
  CefRefPtr<cad::CadApp> app(new cad::CadApp());

  // Every CAD Browser process runs this same code. For anything that is not
  // the browser process, CefExecuteProcess runs it to completion and returns.
  const int exit_code = CefExecuteProcess(main_args, app.get(), sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  // From here on we are the browser process.
  cad::AppState::Create();
  cad::AppState* state = cad::AppState::Get();
  state->PreInitialize();

  cad::SwitchOptions options;
  options.force_hardware_acceleration =
      state->settings().force_hardware_acceleration;
  options.uncapped_frame_rate = state->settings().uncapped_fps_default;
  // Both derived from the hardware read during PreInitialize. They have to be
  // on the command line before CefInitialize, which is why the hardware is
  // read that early.
  options.js_heap_mb = state->RecommendedJsHeapMb();
  options.gpu_memory_mb = state->RecommendedGpuMemoryMb();
  options.onshape_focus = state->settings().onshape_focus_mode;
  app->set_switch_options(options);

  CefSettings settings;
  settings.no_sandbox = sandbox_info ? 0 : 1;
  // Multi-threaded message loop is deliberately off: CEF Views requires all UI
  // work on one thread, and CefRunMessageLoop gives us exactly that.
  settings.multi_threaded_message_loop = 0;

  const std::wstring cache_path = state->CachePath();
  if (!cache_path.empty()) {
    CefString(&settings.cache_path).FromWString(cache_path);
  }
  const std::wstring& user_data = state->user_data_dir();
  if (!user_data.empty()) {
    CefString(&settings.root_cache_path).FromWString(user_data);
    CefString(&settings.log_file).FromWString(user_data + L"\\debug.log");
  }
  settings.log_severity = LOGSEVERITY_WARNING;
  CefString(&settings.locale).FromASCII("en-US");

  if (!CefInitialize(main_args, settings, app.get(), sandbox_info)) {
    cad::AppState::Destroy();
    return CefGetExitCode();
  }

  CefRunMessageLoop();

  state = cad::AppState::Get();
  if (state) {
    state->Shutdown();
  }
  CefShutdown();
  cad::AppState::Destroy();
  return 0;
}

}  // namespace

#if defined(CEF_USE_BOOTSTRAP)

// Entry point called by bootstrap.exe, which is what CADBrowser.exe actually
// is. The sandbox information comes from it already created.
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance,
                                    LPTSTR command_line,
                                    int show_command,
                                    void* sandbox_info,
                                    cef_version_info_t* version_info) {
  return RunMain(instance, command_line, show_command, sandbox_info);
}

#else  // !defined(CEF_USE_BOOTSTRAP)

// Fallback for a build configured without the bootstrap, which also means
// without the sandbox. Not how CAD Browser ships.
int APIENTRY wWinMain(HINSTANCE instance,
                      HINSTANCE previous_instance,
                      LPWSTR command_line,
                      int show_command) {
  UNREFERENCED_PARAMETER(previous_instance);
  return RunMain(instance, command_line, show_command, nullptr);
}

#endif  // !defined(CEF_USE_BOOTSTRAP)
