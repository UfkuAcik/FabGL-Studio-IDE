cmake_minimum_required(VERSION 3.24)

get_filename_component(repository_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT BUILD_DIR)
  message(FATAL_ERROR "BUILD_DIR must name a configured CMake build directory")
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE BASE_DIR "${repository_root}")
set(compile_database "${BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${compile_database}")
  message(FATAL_ERROR
    "Compilation database not found: ${compile_database}. "
    "Configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON.")
endif()

if(CLANG_TIDY_EXECUTABLE)
  if(NOT EXISTS "${CLANG_TIDY_EXECUTABLE}")
    message(FATAL_ERROR
      "CLANG_TIDY_EXECUTABLE does not exist: ${CLANG_TIDY_EXECUTABLE}")
  endif()
else()
  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
endif()

execute_process(
  COMMAND "${CLANG_TIDY_EXECUTABLE}" --verify-config
  WORKING_DIRECTORY "${repository_root}"
  RESULT_VARIABLE config_result
  OUTPUT_VARIABLE config_output
  ERROR_VARIABLE config_error)
if(NOT config_result EQUAL 0)
  message(FATAL_ERROR
    "The repository .clang-tidy configuration is invalid:\n${config_output}${config_error}")
endif()

file(READ "${compile_database}" compile_commands_json)
string(JSON command_count LENGTH "${compile_commands_json}")
if(command_count EQUAL 0)
  message(FATAL_ERROR "Compilation database is empty: ${compile_database}")
endif()

math(EXPR last_command_index "${command_count} - 1")
set(tidy_files "")
foreach(command_index RANGE 0 ${last_command_index})
  string(JSON source_file GET "${compile_commands_json}" ${command_index} file)
  string(JSON command_directory GET "${compile_commands_json}" ${command_index} directory)
  get_filename_component(source_file "${source_file}" ABSOLUTE BASE_DIR "${command_directory}")
  file(RELATIVE_PATH relative_path "${repository_root}" "${source_file}")
  string(REPLACE "\\" "/" relative_path "${relative_path}")

  if(relative_path MATCHES "^\\.\\./")
    continue()
  endif()
  if(NOT relative_path MATCHES
      "^(apps|engine|frameworks|platforms|renderers|tests|tools)/")
    continue()
  endif()
  if(relative_path MATCHES
      "(^|/)(generated|third[-_]?party|out|build|dist|install)(/|$)")
    continue()
  endif()
  if(NOT relative_path MATCHES "\\.(c|cc|cpp|cxx)$")
    continue()
  endif()
  list(APPEND tidy_files "${source_file}")
endforeach()

list(REMOVE_DUPLICATES tidy_files)
list(SORT tidy_files)
if(NOT tidy_files)
  message(FATAL_ERROR
    "No first-party C/C++ translation units were found in ${compile_database}")
endif()

set(failed_files "")
foreach(source_file IN LISTS tidy_files)
  file(RELATIVE_PATH relative_path "${repository_root}" "${source_file}")
  string(REPLACE "\\" "/" relative_path "${relative_path}")
  message(STATUS "clang-tidy: ${relative_path}")
  execute_process(
    COMMAND "${CLANG_TIDY_EXECUTABLE}"
      "-p=${BUILD_DIR}"
      --quiet
      "${source_file}"
    WORKING_DIRECTORY "${repository_root}"
    RESULT_VARIABLE tidy_result
    OUTPUT_VARIABLE tidy_output
    ERROR_VARIABLE tidy_error)
  string(CONCAT tidy_diagnostics "${tidy_output}" "${tidy_error}")
  string(STRIP "${tidy_diagnostics}" tidy_diagnostics)
  if(tidy_diagnostics)
    message("${tidy_diagnostics}")
  endif()
  if(NOT tidy_result EQUAL 0)
    list(APPEND failed_files "${relative_path}")
  endif()
endforeach()

if(failed_files)
  list(JOIN failed_files "\n  " failed_text)
  message(FATAL_ERROR "clang-tidy failed for:\n  ${failed_text}")
endif()

list(LENGTH tidy_files tidy_file_count)
message(STATUS "clang-tidy passed for ${tidy_file_count} first-party translation unit(s)")
