# Wrapper for test_alloc_oom: runs the binary, captures output, checks for the
# expected log line. The binary exits via signal, but this wrapper exits normally
# so CTest can judge it by exit code. Both streams are captured and searched
# together on purpose: s_oom reports on stderr (fprintf, which cannot allocate --
# see bk_alloc.c), while anything routed through SDL_Log lands on stdout.

execute_process(
  COMMAND ${EXE}
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE res
  # Don't treat signal exit as an error; we expect it
)

set(combined "${out}${err}")

if(combined MATCHES "BK: out of memory")
  # The OOM path ran as expected
  message(STATUS "OOM path confirmed: found 'BK: out of memory' in output")
  return()
else()
  # No OOM log found; test fails
  message(FATAL_ERROR "Expected 'BK: out of memory' in output, got:\n${combined}")
endif()
