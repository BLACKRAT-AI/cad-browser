# CAD Browser - top level build.
#
# See PLAN.md for the architecture and CLAUDE.md for the build commands.
# Configure through build.ps1; it locates MSVC and downloads CEF.

cmake_minimum_required(VERSION 3.21)

project(CADBrowser CXX C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(CAD_WARNINGS_AS_ERRORS "Treat warnings as errors in CAD Browser code" ON)
option(CAD_BUILD_TESTS "Build the unit tests" ON)

# ---------------------------------------------------------------------------
# CEF binary distribution
# ---------------------------------------------------------------------------
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef"
    CACHE PATH "CEF binary distribution root")

if(NOT IS_DIRECTORY "${CEF_ROOT}/cmake")
  message(FATAL_ERROR
      "CEF binary distribution not found at:\n  ${CEF_ROOT}\n"
      "Run build.ps1 (it downloads and verifies the pinned distribution).")
endif()

# PLAN.md pins the CEF version. Fail loudly if third_party/cef is something
# else, so a silent version drift can never reach a build.
set(CAD_PINNED_CEF_VERSION "152.0.6")
set(CAD_PINNED_CHROMIUM_VERSION "152.0.7977.83")
file(STRINGS "${CEF_ROOT}/include/cef_version.h" _cad_cef_version_line
     REGEX "^#define CEF_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" _cad_unused "${_cad_cef_version_line}")
set(CAD_ACTUAL_CEF_VERSION "${CMAKE_MATCH_1}")
if(NOT CAD_ACTUAL_CEF_VERSION MATCHES "^${CAD_PINNED_CEF_VERSION}\\+")
  message(FATAL_ERROR
      "CEF version mismatch.\n"
      "  expected: ${CAD_PINNED_CEF_VERSION}+... (chromium ${CAD_PINNED_CHROMIUM_VERSION})\n"
      "  found:    ${CAD_ACTUAL_CEF_VERSION}\n"
      "Update CAD_PINNED_CEF_VERSION here, CLAUDE.md and PLAN.md together.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# CEF defaults to C++17; PLAN.md specifies C++20. CEF clears CMAKE_CXX_FLAGS
# and applies its own lists through SET_*_TARGET_PROPERTIES, so swapping the
# entry here is what actually reaches the compiler.
list(REMOVE_ITEM CEF_CXX_COMPILER_FLAGS "/std:c++17")
list(APPEND CEF_CXX_COMPILER_FLAGS "/std:c++20")

if(NOT CAD_WARNINGS_AS_ERRORS)
  list(REMOVE_ITEM CEF_COMPILER_FLAGS "/WX")
endif()

# Our own warning suppressions, applied on top of CEF's list.
list(APPEND CEF_COMPILER_FLAGS
  /wd4505   # unreferenced local function has been removed
  )

# The imported target for libcef.lib. CEF's own top-level CMakeLists declares
# this, and we do not use that file (it also builds the sample applications),
# so it has to be declared here or "libcef_lib" resolves to a bare library
# name and the link fails.
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# The libcef_dll_wrapper static library that every CEF C++ client links.
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

PRINT_CEF_CONFIG()

message(STATUS "CAD Browser: CEF ${CAD_ACTUAL_CEF_VERSION}")

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(tools)
if(CAD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
