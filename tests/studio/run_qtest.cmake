if(NOT DEFINED TEST_EXECUTABLE OR NOT EXISTS "${TEST_EXECUTABLE}")
    message(FATAL_ERROR "TEST_EXECUTABLE must name an existing Qt test executable")
endif()
if(NOT DEFINED TEST_WORKING_DIRECTORY OR NOT IS_DIRECTORY "${TEST_WORKING_DIRECTORY}")
    message(FATAL_ERROR "TEST_WORKING_DIRECTORY must name an existing directory")
endif()
if(NOT DEFINED TEST_LOG OR TEST_LOG STREQUAL "")
    message(FATAL_ERROR "TEST_LOG must name the Qt test log file")
endif()

file(REMOVE "${TEST_LOG}")
execute_process(
    COMMAND "${TEST_EXECUTABLE}" -o "${TEST_LOG},txt"
    WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr
)

if(NOT test_stdout STREQUAL "")
    message("${test_stdout}")
endif()
if(NOT test_stderr STREQUAL "")
    message("${test_stderr}")
endif()
if(EXISTS "${TEST_LOG}")
    file(READ "${TEST_LOG}" test_log)
    message("${test_log}")
else()
    message("Qt test did not create the expected log: ${TEST_LOG}")
endif()

if(NOT test_result STREQUAL "0")
    message(FATAL_ERROR "Qt test process failed with result ${test_result}")
endif()
