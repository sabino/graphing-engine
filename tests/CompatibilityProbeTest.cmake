file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")

function(require_exit expected)
    execute_process(COMMAND "${PROBE_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT "${result}" STREQUAL "${expected}")
        message(FATAL_ERROR "Probe exit ${result}; expected ${expected}.\n${output}\n${errors}")
    endif()
endfunction()

set(first "${TEST_OUTPUT_DIR}/first.tsv")
set(second "${TEST_OUTPUT_DIR}/second.tsv")
require_exit(0 --cases "${CASES_FILE}" --output "${first}")
require_exit(0 --cases "${CASES_FILE}" --output "${second}")
require_exit(0 --compare "${first}" "${second}")

file(READ "${first}" snapshot)
string(REPLACE "polynomial\tparse.error_code\texact\t0\n" "" truncated "${snapshot}")
if(truncated STREQUAL snapshot)
    message(FATAL_ERROR "Expected parse field missing from test snapshot")
endif()
file(WRITE "${TEST_OUTPUT_DIR}/truncated.tsv" "${truncated}")
require_exit(2 --compare "${first}" "${TEST_OUTPUT_DIR}/truncated.tsv")

string(REPLACE "polynomial\tparse.error_code\texact\t0\n"
    "polynomial\tparse.error_code\texact\t1\n" different "${snapshot}")
file(WRITE "${TEST_OUTPUT_DIR}/different.tsv" "${different}")
require_exit(1 --compare "${first}" "${TEST_OUTPUT_DIR}/different.tsv")

file(READ "${CASES_FILE}" cases)
string(REGEX REPLACE "\nparse_error\t[^\n]*\n" "\n" missing_case "${cases}")
file(WRITE "${TEST_OUTPUT_DIR}/missing-case.tsv" "${missing_case}")
require_exit(2 --cases "${TEST_OUTPUT_DIR}/missing-case.tsv" --output "${TEST_OUTPUT_DIR}/incomplete.tsv")
