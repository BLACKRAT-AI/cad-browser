// CAD Browser - the cad:// scheme.
//
// Every internal page (new tab, settings, system info, the stats overlay) is
// served from resources compiled into the binary. Nothing internal touches the
// disk or the network, which is most of why cold start is fast and why the
// browser works with no connectivity at all.
//
// URL shape:
//   cad://newtab            -> newtab.html
//   cad://settings          -> settings.html
//   cad://newtab/base.css   -> base.css   (shared assets, referenced relatively)
#pragma once

#include <string>

#include "include/cef_scheme.h"

namespace cad {

inline constexpr char kCadScheme[] = "cad";

// Called from CefApp::OnRegisterCustomSchemes in every process. Both the
// browser and the renderer must agree on the scheme's properties.
void RegisterCadCustomScheme(CefRawPtr<CefSchemeRegistrar> registrar);

// Called once after CEF is initialised, on the UI thread.
void RegisterCadSchemeHandlerFactory();

// Maps a cad:// URL to an embedded resource path. Empty when the URL does not
// name one.
std::string ResourcePathForCadUrl(const std::string& url);

}  // namespace cad
