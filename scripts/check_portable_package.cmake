cmake_minimum_required(VERSION 3.24)

if(NOT PACKAGE_DIRECTORY)
  message(FATAL_ERROR "PACKAGE_DIRECTORY must name the CPack output directory")
endif()
get_filename_component(PACKAGE_DIRECTORY "${PACKAGE_DIRECTORY}" ABSOLUTE)
if(NOT IS_DIRECTORY "${PACKAGE_DIRECTORY}")
  message(FATAL_ERROR "Package directory does not exist: ${PACKAGE_DIRECTORY}")
endif()

file(GLOB portable_archives LIST_DIRECTORIES FALSE
  "${PACKAGE_DIRECTORY}/FabGL-Studio-*.zip")
list(LENGTH portable_archives archive_count)
if(NOT archive_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one portable ZIP in ${PACKAGE_DIRECTORY}; found ${archive_count}")
endif()
list(GET portable_archives 0 portable_archive)

set(checksum_file "${portable_archive}.sha256")
if(NOT EXISTS "${checksum_file}")
  message(FATAL_ERROR "CPack SHA-256 file is missing: ${checksum_file}")
endif()
file(READ "${checksum_file}" checksum_text)
string(REGEX MATCH "^[0-9A-Fa-f]+" expected_checksum "${checksum_text}")
string(LENGTH "${expected_checksum}" checksum_length)
if(NOT checksum_length EQUAL 64)
  message(FATAL_ERROR "Invalid SHA-256 file: ${checksum_file}")
endif()
file(SHA256 "${portable_archive}" actual_checksum)
string(TOLOWER "${expected_checksum}" expected_checksum)
string(TOLOWER "${actual_checksum}" actual_checksum)
if(NOT actual_checksum STREQUAL expected_checksum)
  message(FATAL_ERROR
    "Portable ZIP checksum mismatch: expected ${expected_checksum}, got ${actual_checksum}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar tf "${portable_archive}"
  RESULT_VARIABLE list_result
  OUTPUT_VARIABLE archive_listing
  ERROR_VARIABLE list_error)
if(NOT list_result EQUAL 0)
  message(FATAL_ERROR "Portable ZIP could not be listed: ${list_error}")
endif()
string(REPLACE "\\" "/" archive_listing "${archive_listing}")
string(REPLACE "\r\n" "\n" archive_listing "${archive_listing}")
string(REPLACE "\n" "\n/" archive_listing "/${archive_listing}")

set(required_payload
  bin/FabGLStudio.exe
  bin/fabgl_player_pc.exe
  bin/fabgl_asset_compiler.exe
  bin/fabgl_project_cli.exe
  bin/fabgl_toolchain_manager.exe
  bin/Qt6Core.dll
  bin/Qt6Gui.dll
  bin/Qt6Widgets.dll
  bin/platforms/qwindows.dll)
set(missing_payload "")
foreach(relative_path IN LISTS required_payload)
  string(FIND "${archive_listing}" "/${relative_path}" payload_index)
  if(payload_index EQUAL -1)
    list(APPEND missing_payload "${relative_path}")
  endif()
endforeach()
if(missing_payload)
  list(JOIN missing_payload ", " missing_text)
  message(FATAL_ERROR "Portable ZIP is missing required payload: ${missing_text}")
endif()

get_filename_component(archive_name "${portable_archive}" NAME)
message(STATUS "Portable package verified: ${archive_name} (${actual_checksum})")
