cmake_minimum_required(VERSION 3.24)

get_filename_component(repository_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(CLANG_FORMAT_EXECUTABLE)
  if(NOT EXISTS "${CLANG_FORMAT_EXECUTABLE}")
    message(FATAL_ERROR
      "CLANG_FORMAT_EXECUTABLE does not exist: ${CLANG_FORMAT_EXECUTABLE}")
  endif()
else()
  find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)
endif()

set(source_roots
  apps
  engine
  frameworks
  platforms
  renderers
  tests
  tools)
set(source_extensions c cc cpp cxx h hh hpp hxx ino)
set(format_files "")

foreach(source_root IN LISTS source_roots)
  if(NOT IS_DIRECTORY "${repository_root}/${source_root}")
    continue()
  endif()
  foreach(extension IN LISTS source_extensions)
    file(GLOB_RECURSE matching_files LIST_DIRECTORIES FALSE
      "${repository_root}/${source_root}/*.${extension}")
    list(APPEND format_files ${matching_files})
  endforeach()
endforeach()

list(REMOVE_DUPLICATES format_files)
list(SORT format_files)
list(FILTER format_files EXCLUDE REGEX
  "[/\\\\](generated|third[-_]?party|out|build|dist|install)[/\\\\]")

if(NOT format_files)
  message(FATAL_ERROR "No first-party C/C++ source files were found for formatting")
endif()

execute_process(
  COMMAND "${CLANG_FORMAT_EXECUTABLE}" --version
  RESULT_VARIABLE version_result
  OUTPUT_VARIABLE version_output
  ERROR_VARIABLE version_error
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT version_result EQUAL 0)
  message(FATAL_ERROR
    "clang-format could not be executed: ${version_output}${version_error}")
endif()

set(unformatted_files "")
set(first_diagnostic "")
foreach(source_file IN LISTS format_files)
  execute_process(
    COMMAND "${CLANG_FORMAT_EXECUTABLE}"
      --dry-run
      --Werror
      --style=file
      --fallback-style=none
      "${source_file}"
    WORKING_DIRECTORY "${repository_root}"
    RESULT_VARIABLE format_result
    OUTPUT_VARIABLE format_output
    ERROR_VARIABLE format_error)
  if(NOT format_result EQUAL 0)
    file(RELATIVE_PATH relative_path "${repository_root}" "${source_file}")
    string(REPLACE "\\" "/" relative_path "${relative_path}")
    list(APPEND unformatted_files "${relative_path}")
    if(NOT first_diagnostic)
      string(CONCAT first_diagnostic "${format_output}" "${format_error}")
      string(STRIP "${first_diagnostic}" first_diagnostic)
    endif()
  endif()
endforeach()

if(unformatted_files)
  list(JOIN unformatted_files "\n  " unformatted_text)
  message(FATAL_ERROR
    "clang-format check failed for:\n  ${unformatted_text}\n"
    "First diagnostic:\n${first_diagnostic}\n"
    "Run clang-format with the repository .clang-format file and commit the result.")
endif()

list(LENGTH format_files format_file_count)
message(STATUS "${version_output}")
message(STATUS "clang-format check passed for ${format_file_count} first-party file(s)")
