// CAD Browser - the names and shapes of every CefProcessMessage we send.
//
// Two channels only, both renderer to browser unless stated:
//   cad.fps    frame timing from the renderer's requestAnimationFrame probe
//   cad.ui     a cad:// page calling window.cadBrowser.send(name, payload)
//   cad.probe  browser to renderer: turn the frame timing probe on or off
#pragma once

namespace cad::ipc {

// Renderer -> browser. Arguments:
//   0 int     renderer process id
//   1 double  frames per second over the reporting window
//   2 double  median frame time in milliseconds
//   3 double  99th percentile frame time in milliseconds
//   4 double  longest single frame in milliseconds
inline constexpr char kFpsReport[] = "cad.fps";

// Renderer -> browser. Arguments:
//   0 string  event name, e.g. "settings.set"
//   1 string  JSON payload
inline constexpr char kUiEvent[] = "cad.ui";

// Browser -> renderer. Arguments:
//   0 bool    whether the frame timing probe should run
inline constexpr char kSetProbeEnabled[] = "cad.probe";

// Browser -> renderer. Arguments:
//   0 bool    whether onshape.com pages should run in focus mode
inline constexpr char kSetSiteFocus[] = "cad.sitefocus";

// How often the renderer reports, in milliseconds. The browser samples at
// 250 ms, so reporting at the same rate keeps one report per sample.
inline constexpr int kFpsReportIntervalMs = 250;

}  // namespace cad::ipc
