cmake_minimum_required(VERSION 3.24)

get_filename_component(repository_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# These paths are repository-local products of CMake, CTest, packaging, or the
# managed ESP32 bootstrap. They must never be present in a clean source checkout.
set(generated_paths
  CMakeCache.txt
  CMakeFiles
  CTestTestfile.cmake
  Testing
  cmake_install.cmake
  compile_commands.json
  .ninja_deps
  .ninja_log
  build
  out
  dist
  install
  .downloads
  .toolchains)

find_program(GIT_EXECUTABLE NAMES git REQUIRED)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${repository_root}" ls-files --cached
  RESULT_VARIABLE git_result
  OUTPUT_VARIABLE tracked_output
  ERROR_VARIABLE git_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT git_result EQUAL 0)
  message(FATAL_ERROR "git ls-files failed: ${git_error}")
endif()
string(REPLACE "\r\n" "\n" tracked_output "${tracked_output}")
string(REPLACE "\n" ";" tracked_files "${tracked_output}")

set(found "")
foreach(tracked_file IN LISTS tracked_files)
  string(REPLACE "\\" "/" tracked_file "${tracked_file}")
  foreach(generated_path IN LISTS generated_paths)
    if(tracked_file STREQUAL generated_path)
      list(APPEND found "${tracked_file}")
      break()
    endif()
    string(FIND "${tracked_file}" "${generated_path}/" generated_prefix_index)
    if(generated_prefix_index EQUAL 0)
      list(APPEND found "${tracked_file}")
      break()
    endif()
  endforeach()
endforeach()

if(found)
  list(JOIN found ", " found_text)
  message(FATAL_ERROR
    "Generated output is present in the source tree: ${found_text}. "
    "Use an ignored out-of-source build directory and do not commit generated output.")
endif()

message(STATUS "No generated build or managed-toolchain output is tracked by Git")
