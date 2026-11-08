// CAD Browser - the names and shapes of every CefProcessMessage we send.
//
// Two channels only, both renderer to browser unless stated:
//   cad.fps    frame timing from the renderer's requestAnimationFrame probe
//   cad.ui     a cad:// page calling window.cadBrowser.send(name, payload)
//   cad.probe  browser to renderer: turn the frame timing probe on or off
//   cad.capture / cad.captured  browser asks the renderer for the page the
//              user is looking at, renderer answers with its content
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

// Browser -> renderer, to the main frame. Arguments:
//   0 int     request id, echoed back in the result
inline constexpr char kCaptureRequest[] = "cad.capture";

// Renderer -> browser. Arguments:
//   0 int     request id from kCaptureRequest
//   1 bool    true when the page was read
//   2 string  url, or the reason for failure when argument 1 is false
//   3 string  title
//   4 string  document.body.innerText
//   5 string  document.documentElement.outerHTML
//   6 list    links, each a list of [string text, string href]
inline constexpr char kCaptureResult[] = "cad.captured";

// How often the renderer reports, in milliseconds. The browser samples at
// 250 ms, so reporting at the same rate keeps one report per sample.
inline constexpr int kFpsReportIntervalMs = 250;

}  // namespace cad::ipc
