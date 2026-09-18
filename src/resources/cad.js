// CAD Browser - shared helpers for cad:// pages.
//
// window.cadBrowser.send(name, payload) is installed by the renderer for the
// cad:// scheme only. Everything here degrades quietly when it is missing, so
// a page opened in a context without the bridge still renders.

(function () {
  'use strict';

  var send = function (name, payload) {
    if (window.cadBrowser && typeof window.cadBrowser.send === 'function') {
      window.cadBrowser.send(name, payload);
    }
  };

  // ---- GPU probe -----------------------------------------------------------
  //
  // PLAN.md section 4.3 step 3: knowing which adapter Windows *should* use is
  // not the same as knowing which one is drawing. The only way to find that
  // out is to ask a real graphics context, so every internal page reports it
  // once. The browser compares it against the adapter it chose and says so in
  // the menu when they disagree.
  function probeGpu() {
    var result = { renderer: '', vendor: '', api: '' };
    try {
      var canvas = document.createElement('canvas');
      var gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
      if (gl) {
        result.api = 'webgl';
        var info = gl.getExtension('WEBGL_debug_renderer_info');
        if (info) {
          result.renderer = String(gl.getParameter(info.UNMASKED_RENDERER_WEBGL) || '');
          result.vendor = String(gl.getParameter(info.UNMASKED_VENDOR_WEBGL) || '');
        } else {
          // Without the debug extension the masked strings are all we get.
          result.renderer = String(gl.getParameter(gl.RENDERER) || '');
          result.vendor = String(gl.getParameter(gl.VENDOR) || '');
        }
        var lose = gl.getExtension('WEBGL_lose_context');
        if (lose) {
          lose.loseContext();
        }
      }
    } catch (e) {
      result.renderer = '';
    }

    if (result.renderer) {
      send('gpu.probe', result);
    }

    // WebGPU reports the adapter more precisely where it is available. It is
    // asynchronous, so it refines the answer rather than replacing it.
    if (navigator.gpu && typeof navigator.gpu.requestAdapter === 'function') {
      navigator.gpu.requestAdapter().then(function (adapter) {
        if (!adapter || !adapter.info) {
          return;
        }
        var description = [adapter.info.vendor, adapter.info.architecture,
                           adapter.info.device].filter(Boolean).join(' ');
        if (description) {
          send('gpu.probe', { renderer: description, api: 'webgpu' });
        }
      }).catch(function () {});
    }
  }

  // ---- helpers used by the pages ------------------------------------------
  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) {
      node.className = className;
    }
    if (text !== undefined && text !== null) {
      node.textContent = String(text);
    }
    return node;
  }

  function row(parent, label, value, className) {
    var container = el('div', 'row');
    container.appendChild(el('div', 'label', label));
    var valueNode = el('div', 'value' + (className ? ' ' + className : ''));
    if (value instanceof Node) {
      valueNode.appendChild(value);
    } else {
      valueNode.textContent = value === undefined || value === null || value === ''
        ? '—'
        : String(value);
    }
    container.appendChild(valueNode);
    parent.appendChild(container);
    return container;
  }

  function navigate(url) {
    send('navigate', { url: url });
  }

  window.cad = {
    send: send,
    el: el,
    row: row,
    navigate: navigate,
    probeGpu: probeGpu
  };

  // Run the probe once the page is up, off the critical path.
  if (document.readyState === 'complete') {
    setTimeout(probeGpu, 0);
  } else {
    window.addEventListener('load', function () { setTimeout(probeGpu, 0); });
  }
})();
