cmake_minimum_required(VERSION 3.24)

if(NOT PACKAGE_DIRECTORY)
  message(FATAL_ERROR "PACKAGE_DIRECTORY must name the CPack output directory")
endif()
get_filename_component(PACKAGE_DIRECTORY "${PACKAGE_DIRECTORY}" ABSOLUTE)
if(NOT IS_DIRECTORY "${PACKAGE_DIRECTORY}")
  message(FATAL_ERROR "Package directory does not exist: ${PACKAGE_DIRECTORY}")
endif()

file(GLOB portable_archives LIST_DIRECTORIES FALSE
  "${PACKAGE_DIRECTORY}/FabGL-Studio-*-Windows-*.zip")
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
  bin/platforms/qwindows.dll
  bin/platforms/qoffscreen.dll
  include/fabgl/scene/scene.h
  include/fabgl/rendering/framebuffer.h
  include/fabgl/frameworks/platformer.h
  include/fabgl/assets/asset_importer.h
  lib/libfabgl_engine.a
  lib/libfabgl_renderers.a
  lib/libfabgl_frameworks.a
  lib/libfabgl_asset_pipeline.a
  lib/cmake/FabGLStudio/FabGLStudioConfig.cmake
  lib/cmake/FabGLStudio/FabGLStudioConfigVersion.cmake
  lib/cmake/FabGLStudio/FabGLStudioTargets.cmake
  share/doc/FabGLStudio/README.md
  share/doc/FabGLStudio/LICENSE
  share/doc/FabGLStudio/NOTICE
  share/doc/FabGLStudio/THIRD_PARTY_LICENSES.md
  share/doc/FabGLStudio/USER_GUIDE.md
  share/doc/FabGLStudio/BUILDING.md
  share/doc/FabGLStudio/TOOLCHAIN.md
  share/doc/FabGLStudio/docs/FINAL_REPORT.md
  share/fabgl-studio/toolchains/manifest.json
  share/fabgl-studio/toolchains/desktop-manifest.json
  share/fabgl-studio/scripts/bootstrap_desktop.ps1
  share/fabgl-studio/scripts/bootstrap_nsis.ps1
  share/fabgl-studio/scripts/build_desktop.ps1
  share/fabgl-studio/scripts/build_project.ps1
  share/fabgl-studio/scripts/build_project_scripts.ps1
  share/fabgl-studio/scripts/bootstrap_toolchain.ps1
  share/fabgl-studio/scripts/build_esp32.ps1
  share/fabgl-studio/scripts/capture_hardware_diagnostics.ps1
  share/fabgl-studio/scripts/detect_serial_ports.ps1
  share/fabgl-studio/scripts/upload_esp32.ps1
  share/fabgl-studio/scripts/serial_monitor.ps1
  share/fabgl-studio/examples/empty/Empty.fglproject
  share/fabgl-studio/examples/empty/Scenes/Main.fglscene
  share/fabgl-studio/examples/platformer/Platformer.fglproject
  share/fabgl-studio/examples/platformer/Scenes/Main.fglscene
  share/fabgl-studio/examples/top_down/TopDown.fglproject
  share/fabgl-studio/examples/top_down/Scenes/Main.fglscene
  share/fabgl-studio/examples/raycast_fps/RaycastFPS.fglproject
  share/fabgl-studio/examples/raycast_fps/Scenes/Main.fglscene
  share/fabgl-studio/examples/pseudo3d_racer/Racer.fglproject
  share/fabgl-studio/examples/pseudo3d_racer/Scenes/Main.fglscene
  share/fabgl-studio/examples/tps_technology/TPS.fglproject
  share/fabgl-studio/examples/tps_technology/Scenes/Main.fglscene
  share/fabgl-studio/examples/ui_showcase/UIShowcase.fglproject
  share/fabgl-studio/examples/ui_showcase/Scenes/Main.fglscene
  share/fabgl-studio/examples/animation_showcase/AnimationShowcase.fglproject
  share/fabgl-studio/examples/animation_showcase/Scenes/Main.fglscene
  share/fabgl-studio/examples/audio_showcase/AudioShowcase.fglproject
  share/fabgl-studio/examples/audio_showcase/Scenes/Main.fglscene
  share/fabgl-studio/examples/asset_streaming/AssetStreaming.fglproject
  share/fabgl-studio/examples/asset_streaming/Scenes/Main.fglscene)
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
string(REGEX MATCH
  "/lib/cmake/FabGLStudio/FabGLStudioTargets-[^/\n]+\\.cmake"
  configuration_targets "${archive_listing}")
if(configuration_targets STREQUAL "")
  message(FATAL_ERROR
    "Portable ZIP has no configuration-specific FabGLStudio SDK targets file")
endif()

get_filename_component(archive_name "${portable_archive}" NAME)
message(STATUS "Portable package verified: ${archive_name} (${actual_checksum})")
