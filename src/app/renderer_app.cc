#include "sys/win_util.h"

#include "app/renderer_app.h"

#include "browser/url_util.h"
#include "ui/theme.h"
#include "include/cef_command_line.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "ipc/messages.h"

namespace cad {
namespace {

// The property that carries the native report callback into the closure. The
// script deletes it on the next line, so nothing of ours survives on `window`.
constexpr char kProbeCarrier[] = "__cadFpsReport";

// Frame timing, measured from requestAnimationFrame.
//
// report() returns false when the browser has switched the probe off, which is
// how the loop stops: no timer to clear, no listener to remove, and nothing
// left running on a page once the overlay is closed.
constexpr char kProbeScript[] = R"JS(
(function (report) {
  if (typeof report !== 'function' || typeof requestAnimationFrame !== 'function') {
    return;
  }
  var intervals = [];
  var last = 0;
  var windowStart = 0;
  function tick(now) {
    if (last !== 0) {
      var delta = now - last;
      // Ignore absurd gaps: a backgrounded tab or a debugger pause is not a
      // frame time anyone wants averaged in.
      if (delta > 0 && delta < 2000) {
        intervals.push(delta);
      }
    }
    last = now;
    if (windowStart === 0) {
      windowStart = now;
    }
    var elapsed = now - windowStart;
    if (elapsed >= 250) {
      var fps = 0, p50 = 0, p99 = 0, longest = 0;
      if (intervals.length > 0) {
        var sorted = intervals.slice().sort(function (a, b) { return a - b; });
        p50 = sorted[Math.floor(sorted.length * 0.5)];
        p99 = sorted[Math.min(sorted.length - 1,
                              Math.floor(sorted.length * 0.99))];
        longest = sorted[sorted.length - 1];
        fps = (intervals.length * 1000) / elapsed;
      }
      if (report(fps, p50, p99, longest) === false) {
        return;
      }
      intervals.length = 0;
      windowStart = now;
    }
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
})(window.__cadFpsReport);
delete window.__cadFpsReport;
)JS";

// Onshape focus mode.
//
// Onshape spends a large part of the window on chrome that is needed a few
// seconds at a time: a document bar, an icon rail, an element toolbar and two
// tab strips. This collapses all of it so the viewport fills the window, and
// puts a small handle on the middle of the top edge to toggle it back.
//
// The feature tree is deliberately left alone. Onshape already has its own
// control for collapsing it, and hiding it too took away somewhere people
// work rather than something in the way.
//
// An earlier version revealed the chrome automatically when the pointer
// neared an edge. A visible control the person presses is better here: it
// costs no mousemove handler on a page whose main thread is already the
// bottleneck, it cannot surprise anyone mid-drag, and it cannot strand them
// the way a broken reveal would.
constexpr char kSiteFocusScript[] = R"JS(
(function () {
  if (window.__cadSiteFocus) { return; }
  if (!/(^|\.)onshape\.com$/i.test(location.hostname)) { return; }

  // Never cached: this script runs before the document that replaces it has
  // parsed, and the <html> element it would capture is thrown away.
  function html() { return document.documentElement; }

  var enabled = false;
  var hidden = true;
  var observing = false;
  var handle = null;
  var resizeTimers = [];

  // What Onshape puts around the viewport, measured against a live document
  // rather than guessed. The feature tree is deliberately not in this list:
  // Onshape already has its own control for collapsing it, and taking it away
  // as well removed something people work in rather than something in the way.
  //
  // Hiding with display:none rather than sliding the panels aside is also
  // deliberate. They are positioned by Onshape's own splitters from inline
  // styles, so moving them left the rest of the page offset instead of
  // reflowing into the space they freed.
  var CSS = [
    'html.cadbHide nav.navbar,',
    'html.cadbHide os-vue-panel-selector-component,',
    'html.cadbHide #os-tab-bar,',
    'html.cadbHide #osToolbarContainer,',
    'html.cadbHide #tab-menu-container {',
    '  display: none !important;',
    '}',
    // The navbar is position:fixed inside a container that reserves its
    // height, and the content container is fixed at top:40px. Both have to be
    // collapsed or the top strip stays empty.
    'html.cadbHide .navbar-container {',
    '  height: 0 !important; min-height: 0 !important;',
    '}',
    'html.cadbHide .os-content-container {',
    '  top: 0 !important; height: 100% !important;',
    '}',
    // These two carry inline geometry written by Onshape's splitters, which
    // still describes a toolbar and a tab bar that are no longer there.
    'html.cadbHide .element-content-splitter-container,',
    'html.cadbHide .element-split-container {',
    '  top: 0 !important; height: 100% !important;',
    '}',
    // The handle itself. Flush with the very top of the window, never hidden
    // and never moved, because it is the only way back and a control that
    // shifts position is worse than one in an awkward place. Two earlier
    // versions were wrong: a fixed 66px offset left it hanging in mid-page,
    // and making it step aside when this browser's own toolbar appeared made
    // it move around. The toolbar keeps clear of it instead; see
    // MainWindow::UpdateChromeFromPointer.
    '#cadb-handle {',
    '  position: fixed !important; left: 50% !important;',
    '  top: 0 !important;',
    '  transform: translateX(-50%) !important;',
    '  z-index: 2147483000 !important;',
    '  width: 76px !important; height: 22px !important;',
    '  margin: 0 !important; padding: 0 !important;',
    '  display: flex !important;',
    '  align-items: center !important; justify-content: center !important;',
    '  background: #202325 !important; color: #C9CED3 !important;',
    // A transparent top border rather than none: the box then has the same
    // border on both sides and the chevron sits on the exact middle. With
    // border-top:none it was a pixel high.
    '  border: 1px solid #35393D !important;',
    '  border-top-color: transparent !important;',
    '  box-sizing: border-box !important;',
    '  border-radius: 0 0 6px 6px !important;',
    '  box-shadow: 0 2px 8px rgba(0,0,0,.35) !important;',
    '  line-height: 0 !important;',
    '  cursor: pointer !important; user-select: none !important;',
    '  opacity: .55 !important;',
    '  transition: opacity 120ms ease, background 120ms ease !important;',
    '}',
    '#cadb-handle:hover {',
    '  opacity: 1 !important; background: #30353A !important;',
    '}',
    // The chevron is drawn rather than typed. A font glyph was tried first and
    // the two arrows did not sit at the same height as each other: U+2303 and
    // U+2304 have different metrics in Segoe UI, so the mark drifted towards
    // an edge when the state changed. A path whose ink is symmetric about the
    // middle of its own viewBox is centred in both states by construction.
    '#cadb-handle svg {',
    '  display: block !important; width: 22px !important; height: 14px !important;',
    '  pointer-events: none !important;',
    '  transition: transform 120ms ease !important;',
    '}',
    '#cadb-handle.cadbUp svg { transform: rotate(180deg) !important; }'
  ].join('\n');

  function ensureStyle() {
    var head = document.head;
    if (!head) { return; }
    if (head.querySelector('style[data-cad-browser="focus"]')) { return; }
    var el = document.createElement('style');
    el.setAttribute('data-cad-browser', 'focus');
    el.textContent = CSS;
    head.appendChild(el);
  }

  // Onshape re-measures its canvas from its own splitters rather than from
  // the window, and it does not settle within one frame of the layout
  // changing. One dispatch after the change moves the panes; a second, later
  // one is what makes the WebGL buffer follow.
  function resizeSoon() {
    while (resizeTimers.length) { clearTimeout(resizeTimers.pop()); }
    [120, 800].forEach(function (delay) {
      resizeTimers.push(setTimeout(function () {
        window.dispatchEvent(new Event('resize'));
      }, delay));
    });
  }

  // Drawn rather than typed. A font glyph was tried first and the two arrows
  // did not sit at the same height as each other: U+2303 and U+2304 have
  // different metrics in Segoe UI, so the mark drifted towards an edge when
  // the state changed. Here the viewBox is 24 by 16 and the ink runs from
  // y=3.5 to y=12.5, so its centre is the centre of the box. Rotating it by
  // half a turn lands in exactly the same place, which is what keeps it
  // centred in both states rather than only in one.
  var CHEVRON =
    '<svg viewBox="0 0 24 16" aria-hidden="true" focusable="false">' +
    '<path d="M5 5 L12 11 L19 5" fill="none" stroke="currentColor" ' +
    'stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>' +
    '</svg>';

  function paintHandle() {
    if (!handle) { return; }
    if (handle.innerHTML.indexOf('svg') === -1) { handle.innerHTML = CHEVRON; }
    handle.classList.toggle('cadbUp', !hidden);
    handle.title = hidden ? 'Show Onshape panels' : 'Hide Onshape panels';
    handle.setAttribute('aria-label', handle.title);
    handle.setAttribute('aria-pressed', hidden ? 'true' : 'false');
  }

  function ensureHandle() {
    if (!document.body) { return; }
    if (handle && handle.isConnected) { return; }
    handle = document.getElementById('cadb-handle');
    if (!handle) {
      handle = document.createElement('div');
      handle.id = 'cadb-handle';
      handle.setAttribute('role', 'button');
      handle.setAttribute('tabindex', '0');
      handle.addEventListener('click', function (e) {
        e.preventDefault();
        e.stopPropagation();
        toggle();
      }, true);
      handle.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          toggle();
        }
      }, true);
    }
    document.body.appendChild(handle);
    paintHandle();
  }

  function paint() {
    var want = enabled && hidden;
    var list = html().classList;
    if (list.contains('cadbHide') !== want) {
      list.toggle('cadbHide', want);
      resizeSoon();
    }
    paintHandle();
  }

  function toggle() {
    hidden = !hidden;
    try { sessionStorage.setItem('cadbHidden', hidden ? '1' : '0'); } catch (ignored) {}
    paint();
  }

  function reapply() {
    if (!enabled) { return; }
    ensureStyle();
    ensureHandle();
    paint();
    // Attached here rather than at startup, because the elements to watch do
    // not exist yet when this script runs. Onshape sets its own classes on
    // <html> after load, and replaces the children of <body> when it moves
    // between tabs; either would otherwise take our work away. Both observers
    // are attribute- or direct-child-only, so neither walks the tree.
    if (!observing) {
      try {
        new MutationObserver(function () {
          if (html().classList.contains('cadbHide') !== (enabled && hidden)) {
            html().classList.toggle('cadbHide', enabled && hidden);
          }
        }).observe(html(), { attributes: true, attributeFilter: ['class'] });
        if (document.body) {
          new MutationObserver(function () {
            ensureHandle();
          }).observe(document.body, { childList: true });
        }
        observing = true;
      } catch (ignored) {}
    }
  }

  Object.defineProperty(window, '__cadSiteFocus', {
    value: Object.freeze({
      set: function (on) {
        enabled = !!on;
        if (enabled) {
          try {
            var saved = sessionStorage.getItem('cadbHidden');
            if (saved !== null) { hidden = saved === '1'; }
          } catch (ignored) {}
          reapply();
        } else {
          html().classList.remove('cadbHide');
          if (handle && handle.parentNode) { handle.parentNode.removeChild(handle); }
          handle = null;
          resizeSoon();
        }
      }
    }),
    writable: false,
    configurable: false,
    enumerable: false
  });

  // This script runs before the document that will replace it has parsed, so
  // the stylesheet, the class on <html> and the handle are all discarded.
  // Re-applying on each readiness step is what makes the mode survive the
  // page it is meant to change.
  document.addEventListener('readystatechange', reapply, true);
  document.addEventListener('DOMContentLoaded', reapply, true);
  window.addEventListener('load', reapply, true);
})();
)JS";

constexpr char kSiteFocusOn[] =
    "window.__cadSiteFocus && window.__cadSiteFocus.set(true);";
constexpr char kSiteFocusOff[] =
    "window.__cadSiteFocus && window.__cadSiteFocus.set(false);";

// onshape.com and its subdomains. Matching on the registrable domain rather
// than on a substring so that a host merely containing the word cannot pull
// the treatment in.
bool IsOnshapeUrl(const std::string& url) {
  return RegistrableDomain(HostFromUrl(url)) == "onshape.com";
}

// The cad:// bridge. One function, no page-visible state beyond it.
constexpr char kBridgeScript[] = R"JS(
(function (send) {
  Object.defineProperty(window, 'cadBrowser', {
    value: Object.freeze({
      send: function (name, payload) {
        send(String(name), payload === undefined ? '' :
             (typeof payload === 'string' ? payload : JSON.stringify(payload)));
      }
    }),
    writable: false,
    configurable: false,
    enumerable: false
  });
})(window.__cadSend);
delete window.__cadSend;
)JS";

// Sends the frame-timing message. Returns false to the page when the probe has
// been switched off.
class FrameProbeHandler : public CefV8Handler {
 public:
  explicit FrameProbeHandler(RendererApp* app) : app_(app) {}

  bool Execute(const CefString& name,
               CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval,
               CefString& exception) override {
    const bool enabled = app_ && app_->probe_enabled();
    retval = CefV8Value::CreateBool(enabled);
    if (!enabled || arguments.size() < 4) {
      return true;
    }

    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    if (!context) {
      return true;
    }
    CefRefPtr<CefFrame> frame = context->GetFrame();
    if (!frame) {
      return true;
    }

    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(ipc::kFpsReport);
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetInt(0, static_cast<int>(::GetCurrentProcessId()));
    args->SetDouble(1, arguments[0]->GetDoubleValue());
    args->SetDouble(2, arguments[1]->GetDoubleValue());
    args->SetDouble(3, arguments[2]->GetDoubleValue());
    args->SetDouble(4, arguments[3]->GetDoubleValue());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

 private:
  RendererApp* const app_;

  IMPLEMENT_REFCOUNTING(FrameProbeHandler);
  DISALLOW_COPY_AND_ASSIGN(FrameProbeHandler);
};

// Forwards window.cadBrowser.send to the browser process.
class BridgeHandler : public CefV8Handler {
 public:
  BridgeHandler() = default;

  bool Execute(const CefString& name,
               CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval,
               CefString& exception) override {
    if (arguments.size() < 2) {
      exception = "cadBrowser.send requires a name and a payload";
      return true;
    }
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    if (!context) {
      return true;
    }
    CefRefPtr<CefFrame> frame = context->GetFrame();
    if (!frame) {
      return true;
    }

    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(ipc::kUiEvent);
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetString(0, arguments[0]->GetStringValue());
    args->SetString(1, arguments[1]->GetStringValue());
    frame->SendProcessMessage(PID_BROWSER, message);
    retval = CefV8Value::CreateBool(true);
    return true;
  }

 private:
  IMPLEMENT_REFCOUNTING(BridgeHandler);
  DISALLOW_COPY_AND_ASSIGN(BridgeHandler);
};

void RunScript(CefRefPtr<CefV8Context> context, const char* script) {
  CefRefPtr<CefV8Value> retval;
  CefRefPtr<CefV8Exception> exception;
  context->Eval(script, CefString(), 0, retval, exception);
}

}  // namespace

RendererApp::RendererApp() = default;

void RendererApp::InstallFrameProbe(CefRefPtr<CefV8Context> context) {
  if (!context || !context->Enter()) {
    return;
  }
  CefRefPtr<CefV8Value> global = context->GetGlobal();
  if (global) {
    CefRefPtr<CefV8Value> fn =
        CefV8Value::CreateFunction(kProbeCarrier, new FrameProbeHandler(this));
    global->SetValue(kProbeCarrier, fn, V8_PROPERTY_ATTRIBUTE_DONTENUM);
    RunScript(context, kProbeScript);
  }
  context->Exit();
}

void RendererApp::InstallSiteFocus(CefRefPtr<CefV8Context> context) {
  if (!context || !context->Enter()) {
    return;
  }
  // The handle has to clear this browser's own floating chrome, which is a
  // native overlay above the whole page rather than part of it. The height is
  // substituted rather than written twice so the two cannot drift apart.
  static const std::string script = [] {
    std::string text = kSiteFocusScript;
    const std::string token = "__CHROME_H__";
    const size_t at = text.find(token);
    if (at != std::string::npos) {
      text.replace(at, token.size(), std::to_string(metrics::kChromeHeight));
    }
    return text;
  }();
  RunScript(context, script.c_str());
  RunScript(context, site_focus_enabled_ ? kSiteFocusOn : kSiteFocusOff);
  context->Exit();
}

void RendererApp::InstallCadBridge(CefRefPtr<CefV8Context> context) {
  if (!context || !context->Enter()) {
    return;
  }
  CefRefPtr<CefV8Value> global = context->GetGlobal();
  if (global) {
    CefRefPtr<CefV8Value> fn =
        CefV8Value::CreateFunction("__cadSend", new BridgeHandler());
    global->SetValue("__cadSend", fn, V8_PROPERTY_ATTRIBUTE_DONTENUM);
    RunScript(context, kBridgeScript);
  }
  context->Exit();
}

void RendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefV8Context> context) {
  if (!frame || !context) {
    return;
  }

  if (!site_focus_read_) {
    site_focus_read_ = true;
    CefRefPtr<CefCommandLine> command_line =
        CefCommandLine::GetGlobalCommandLine();
    site_focus_enabled_ =
        command_line && command_line->HasSwitch("cad-onshape-focus");
  }

  const std::string url = frame->GetURL().ToString();
  if (IsInternalUrl(url)) {
    InstallCadBridge(context);
  }

  // Site focus is a whole-page treatment, so only the main frame gets it.
  // The host test is repeated inside the script; this one just avoids
  // evaluating anything at all on the rest of the web.
  if (frame->IsMain() && IsOnshapeUrl(url)) {
    InstallSiteFocus(context);
  }

  // Frame timing is only meaningful for the page's own main frame, and only
  // when someone is looking at the overlay.
  if (probe_enabled_ && frame->IsMain()) {
    InstallFrameProbe(context);
  }
}

bool RendererApp::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  if (!message) {
    return false;
  }
  const std::string name = message->GetName().ToString();
  if (name != ipc::kSetProbeEnabled && name != ipc::kSetSiteFocus) {
    return false;
  }
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (!args || args->GetSize() < 1) {
    return false;
  }

  if (name == ipc::kSetSiteFocus) {
    site_focus_enabled_ = args->GetBool(0);
    site_focus_read_ = true;
    if (frame && frame->IsMain() && IsOnshapeUrl(frame->GetURL().ToString())) {
      // The controller installs itself once and is idempotent afterwards, so
      // this both sets up a page that predates the setting and re-points one
      // that already has it.
      InstallSiteFocus(frame->GetV8Context());
    }
    return true;
  }

  const bool enabled = args->GetBool(0);
  const bool was_enabled = probe_enabled_;
  probe_enabled_ = enabled;

  // Turning it on has to reach pages that are already loaded; turning it off
  // needs no action, because the running loop stops itself at its next frame
  // when report() returns false.
  if (enabled && !was_enabled && frame && frame->IsMain()) {
    InstallFrameProbe(frame->GetV8Context());
  }
  return true;
}

}  // namespace cad
