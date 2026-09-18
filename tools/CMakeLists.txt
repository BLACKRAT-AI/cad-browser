# CAD Browser - developer tools.

# dump_sysinfo prints everything the sys/ layer detects. It is the acceptance
# tool for milestone M1: its output has to match Task Manager, dxdiag and the
# BIOS before any browser UI work is trusted.
add_executable(dump_sysinfo dump_sysinfo.cc)
SET_EXECUTABLE_TARGET_PROPERTIES(dump_sysinfo)
target_link_libraries(dump_sysinfo PRIVATE cad_sys)
set_target_properties(dump_sysinfo PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CAD_OUT_DIR}")
