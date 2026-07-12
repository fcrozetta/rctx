# Integration check for the top-level catch (issue #10): a handler failure must
# exit 1 with an "error:" message, not abort (rc 134 / "terminating due to
# uncaught exception"). Driven with a --db that exists but is not a sqlite file.
execute_process(
  COMMAND "${RCTX_BIN}" query anything --db "${CMAKE_CURRENT_LIST_DIR}/fixtures/not-a-db.txt"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)
set(combined "${out}${err}")
if(NOT code EQUAL 1)
  message(FATAL_ERROR "expected exit 1, got '${code}'. output:\n${combined}")
endif()
if(NOT combined MATCHES "error:")
  message(FATAL_ERROR "expected an 'error:' line on stderr. output:\n${combined}")
endif()
if(combined MATCHES "terminating")
  message(FATAL_ERROR "process aborted instead of exiting cleanly. output:\n${combined}")
endif()
message(STATUS "cli error path exits cleanly (rc=${code})")
