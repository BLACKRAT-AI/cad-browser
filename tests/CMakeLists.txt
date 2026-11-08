# CAD Browser - unit tests.
#
# Deliberately dependency-free: a ~100 line harness in cad_test.h beats
# vendoring a framework for the handful of things here that are worth testing
# in isolation (SMBIOS parsing, byte formatting, discard policy).

add_executable(cad_tests
  cad_test.h
  test_main.cc
  util_test.cc
  smbios_test.cc
  smbios_fixtures.h
  smbios_fixtures.cc
  blocklist_test.cc
  url_util_test.cc
  json_test.cc
  page_capture_test.cc
  discard_policy_test.cc
  )
SET_EXECUTABLE_TARGET_PROPERTIES(cad_tests)
target_include_directories(cad_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(cad_tests PRIVATE cad_core cad_sys)
set_target_properties(cad_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CAD_OUT_DIR}")

add_test(NAME cad_tests COMMAND cad_tests)
