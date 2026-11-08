# CAD Browser - application targets.
#
# Four pieces:
#   cad_sys        hardware and OS facts, no CEF
#   cad_core       browser logic worth unit testing, no CEF
#   cad_resources  the cad:// pages, compiled into the binary
#   CADBrowser     the CEF-facing DLL that bootstrap.exe loads

# All build products land in <repo>/out/<config> so the run commands in
# CLAUDE.md stay stable.
set(CAD_OUT_DIR "${CMAKE_SOURCE_DIR}/out/${CMAKE_BUILD_TYPE}")
set(CAD_OUT_DIR "${CAD_OUT_DIR}" PARENT_SCOPE)

# ---------------------------------------------------------------------------
# cad_sys
# ---------------------------------------------------------------------------
add_library(cad_sys STATIC
  sys/win_util.h
  sys/win_util.cc
  sys/smbios.h
  sys/smbios.cc
  sys/memory_info.h
  sys/memory_info.cc
  sys/wmi_memory.h
  sys/wmi_memory.cc
  sys/gpu_info.h
  sys/gpu_info.cc
  sys/gpu_preference.h
  sys/gpu_preference.cc
  sys/cpu_info.h
  sys/cpu_info.cc
  sys/os_info.h
  sys/os_info.cc
  sys/perf_counters.h
  sys/perf_counters.cc
  sys/process_tree.h
  sys/process_tree.cc
  sys/stats.h
  sys/stats_sampler.h
  sys/stats_sampler.cc
  )
SET_LIBRARY_TARGET_PROPERTIES(cad_sys)
target_include_directories(cad_sys PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cad_sys PUBLIC
  dxgi.lib
  pdh.lib
  psapi.lib
  shell32.lib
  advapi32.lib
  ole32.lib
  oleaut32.lib
  wbemuuid.lib
  version.lib
  # The CEF target properties add /DELAYLOAD to every executable we build, and
  # the delay-load thunk lives in delayimp.lib.
  delayimp.lib
  )

# ---------------------------------------------------------------------------
# cad_core
# ---------------------------------------------------------------------------
add_library(cad_core STATIC
  browser/json.h
  browser/json.cc
  browser/url_util.h
  browser/url_util.cc
  browser/blocklist.h
  browser/blocklist.cc
  browser/discard_policy.h
  browser/discard_policy.cc
  browser/settings_store.h
  browser/settings_store.cc
  browser/page_capture.h
  browser/page_capture.cc
  )
SET_LIBRARY_TARGET_PROPERTIES(cad_core)
target_include_directories(cad_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cad_core PUBLIC cad_sys)

# ---------------------------------------------------------------------------
# cad_resources - the cad:// pages, embedded
# ---------------------------------------------------------------------------
set(CAD_RESOURCE_FILES
  base.css
  cad.js
  newtab.html
  settings.html
  sysinfo.html
  overlay.html
  about.html
  history.html
  downloads.html
  gpuprobe.html
  blocklists/hosts.txt
  )

set(CAD_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/resources")
set(CAD_GENERATED_CC "${CAD_GENERATED_DIR}/generated_resources.cc")
set(CAD_GENERATED_H "${CAD_GENERATED_DIR}/generated_resources.h")
file(MAKE_DIRECTORY "${CAD_GENERATED_DIR}")

set(CAD_RESOURCE_ABS "")
foreach(_r IN LISTS CAD_RESOURCE_FILES)
  list(APPEND CAD_RESOURCE_ABS "${CMAKE_CURRENT_SOURCE_DIR}/resources/${_r}")
endforeach()

# "|" rather than ";" so the list survives being passed through -D.
string(REPLACE ";" "|" CAD_RESOURCE_ARG "${CAD_RESOURCE_FILES}")

add_custom_command(
  OUTPUT "${CAD_GENERATED_CC}" "${CAD_GENERATED_H}"
  COMMAND "${CMAKE_COMMAND}"
          "-DCAD_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/resources"
          "-DCAD_FILES=${CAD_RESOURCE_ARG}"
          "-DCAD_OUT_CC=${CAD_GENERATED_CC}"
          "-DCAD_OUT_H=${CAD_GENERATED_H}"
          -P "${CMAKE_SOURCE_DIR}/cmake/embed_resources.cmake"
  DEPENDS ${CAD_RESOURCE_ABS} "${CMAKE_SOURCE_DIR}/cmake/embed_resources.cmake"
  COMMENT "Embedding cad:// resources"
  VERBATIM
  )

add_library(cad_resources STATIC "${CAD_GENERATED_CC}" "${CAD_GENERATED_H}")
SET_LIBRARY_TARGET_PROPERTIES(cad_resources)
# The generated source includes itself as "resources/generated_resources.h".
target_include_directories(cad_resources PUBLIC "${CMAKE_BINARY_DIR}/generated")

# ---------------------------------------------------------------------------
# CADBrowser - the client DLL loaded by bootstrap.exe.
#
# CEF 144 supplies the Windows sandbox through bootstrap.exe: the executable is
# a renamed copy of Release/bootstrap.exe and it loads <exe basename>.dll and
# calls the exported RunWinMain. That keeps the full sandbox without linking
# cef_sandbox.lib, which would pin the build to one exact MSVC toolset.
# ---------------------------------------------------------------------------
add_library(CADBrowser SHARED
  app/switches.h
  app/switches.cc
  app/app_state.h
  app/app_state.cc
  app/cad_app.h
  app/cad_app.cc
  app/renderer_app.h
  app/renderer_app.cc
  app/main_win.cc
  ipc/messages.h
  browser/string_resource_handler.h
  browser/string_resource_handler.cc
  browser/scheme_handler.h
  browser/scheme_handler.cc
  browser/request_filter.h
  browser/request_filter.cc
  browser/client_handler.h
  browser/client_handler.cc
  browser/tab.h
  browser/tab.cc
  browser/tab_manager.h
  browser/tab_manager.cc
  browser/session.h
  browser/session.cc
  ui/theme.h
  ui/theme.cc
  ui/tab_strip.h
  ui/tab_strip.cc
  ui/toolbar.h
  ui/toolbar.cc
  ui/hamburger_menu.h
  ui/hamburger_menu.cc
  ui/overlay_view.h
  ui/overlay_view.cc
  ui/main_window.h
  ui/capture_status.h
  ui/capture_status.cc
  ui/main_window.cc
  ui/main_window_capture.cc
  )
SET_LIBRARY_TARGET_PROPERTIES(CADBrowser)
add_dependencies(CADBrowser libcef_dll_wrapper)
target_include_directories(CADBrowser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(CADBrowser PRIVATE
  libcef_lib
  libcef_dll_wrapper
  cad_core
  cad_sys
  cad_resources
  ${CEF_STANDARD_LIBS}
  )

set_target_properties(CADBrowser PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CAD_OUT_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${CAD_OUT_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  )

# bootstrap.exe becomes CADBrowser.exe, and the CEF runtime sits beside it.
add_custom_command(TARGET CADBrowser POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CEF_BINARY_DIR}/bootstrap.exe" "${CAD_OUT_DIR}/CADBrowser.exe"
  VERBATIM)

COPY_FILES(CADBrowser "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CAD_OUT_DIR}")
COPY_FILES(CADBrowser "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CAD_OUT_DIR}")
