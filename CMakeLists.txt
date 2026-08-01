cmake_minimum_required(VERSION 3.24)

project(
  FabGLStudio
  VERSION 0.1.0
  DESCRIPTION "A desktop game studio and portable runtime for Olimex ESP32-SBC-FabGL"
  HOMEPAGE_URL "https://github.com/fabgl-studio/fabgl-studio"
  LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
include(cmake/FGLWarnings.cmake)
include(cmake/FGLSanitizers.cmake)

option(FGL_BUILD_STUDIO "Build the Qt 6 desktop editor when Qt is available" ON)
option(FGL_BUILD_PLAYER "Build the portable PC player" ON)
option(FGL_BUILD_TOOLS "Build command-line authoring and asset tools" ON)
option(FGL_BUILD_TESTS "Build automated tests" ON)
option(FGL_BUILD_EXAMPLES "Build example projects" ON)
option(FGL_WARNINGS_AS_ERRORS "Treat first-party compiler warnings as errors" ON)
option(FGL_ENABLE_SANITIZERS "Enable supported runtime sanitizers" OFF)
option(FGL_PACKAGE_NSIS "Enable the optional Windows NSIS package when makensis is available" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(engine)
add_subdirectory(renderers)
add_subdirectory(frameworks)

if(FGL_BUILD_PLAYER OR FGL_BUILD_STUDIO)
  add_subdirectory(apps)
endif()

if(FGL_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
  add_subdirectory(tools)
endif()

if(BUILD_TESTING AND FGL_BUILD_TESTS)
  add_subdirectory(tests)
endif()

set(FGL_PUBLIC_DOCUMENTS
  README.md
  ROADMAP.md
  ARCHITECTURE.md
  BUILDING.md
  USER_GUIDE.md
  EDITOR_GUIDE.md
  SCRIPTING_API.md
  ENGINE_API.md
  FILE_FORMATS.md
  ASSET_PIPELINE.md
  RENDERERS.md
  PERFORMANCE_BUDGETS.md
  PLUGIN_DEVELOPMENT.md
  PACKAGE_FORMAT.md
  CONTRIBUTING.md
  CODE_OF_CONDUCT.md
  SECURITY.md
  CHANGELOG.md
  THIRD_PARTY_LICENSES.md
  NOTICE
  LICENSE)

foreach(optional_document IN ITEMS HARDWARE_TESTING.md TOOLCHAIN.md)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${optional_document}")
    list(APPEND FGL_PUBLIC_DOCUMENTS "${optional_document}")
  endif()
endforeach()

install(FILES ${FGL_PUBLIC_DOCUMENTS}
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        COMPONENT Documentation)
install(DIRECTORY docs/
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
        COMPONENT Documentation
        FILES_MATCHING PATTERN "*.md")
install(DIRECTORY examples/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/fabgl-studio/examples
        COMPONENT Examples)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/toolchains/manifest.json")
  install(FILES toolchains/manifest.json
          DESTINATION ${CMAKE_INSTALL_DATADIR}/fabgl-studio/toolchains
          COMPONENT ToolchainMetadata)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platforms/fabgl/firmware")
  install(DIRECTORY platforms/fabgl/firmware/
          DESTINATION ${CMAKE_INSTALL_DATADIR}/fabgl-studio/platforms/fabgl/firmware
          COMPONENT FirmwareSource)
endif()
set(FGL_TOOLCHAIN_SCRIPTS
  scripts/bootstrap_toolchain.ps1
  scripts/build_esp32.ps1
  scripts/upload_esp32.ps1
  scripts/serial_monitor.ps1)
foreach(toolchain_script IN LISTS FGL_TOOLCHAIN_SCRIPTS)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${toolchain_script}")
    install(FILES "${toolchain_script}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/fabgl-studio/scripts
            COMPONENT ToolchainMetadata)
  endif()
endforeach()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/hardware")
  install(DIRECTORY tests/hardware/
          DESTINATION ${CMAKE_INSTALL_DATADIR}/fabgl-studio/tests/hardware
          COMPONENT FirmwareSource)
endif()

if(WIN32 AND TARGET fabgl_studio)
  get_target_property(FGL_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  if(FGL_QMAKE_EXECUTABLE)
    get_filename_component(FGL_QT_BIN_DIRECTORY "${FGL_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(FGL_WINDEPLOYQT_EXECUTABLE
      NAMES windeployqt
      HINTS "${FGL_QT_BIN_DIRECTORY}"
      NO_DEFAULT_PATH)
  endif()
  if(FGL_WINDEPLOYQT_EXECUTABLE)
    install(CODE
      "execute_process(COMMAND \"${FGL_WINDEPLOYQT_EXECUTABLE}\" --no-translations --no-system-d3d-compiler --no-opengl-sw \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/FabGLStudio.exe\" RESULT_VARIABLE deploy_result)\nif(NOT deploy_result EQUAL 0)\n  message(FATAL_ERROR \"windeployqt failed with exit code \${deploy_result}\")\nendif()"
      COMPONENT Studio)
  else()
    message(WARNING
      "windeployqt was not found; the installed editor will require a matching Qt runtime. "
      "Do not publish the graphical package until a clean-machine smoke test passes.")
  endif()
endif()

set(CPACK_PACKAGE_NAME "FabGL Studio")
set(CPACK_PACKAGE_VENDOR "FabGL Studio Contributors")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "FabGL Studio")
set(CPACK_PACKAGE_FILE_NAME "FabGL-Studio-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_GENERATOR "ZIP")
if(WIN32 AND FGL_PACKAGE_NSIS)
  find_program(FGL_MAKENSIS_EXECUTABLE NAMES makensis)
  if(FGL_MAKENSIS_EXECUTABLE)
    list(APPEND CPACK_GENERATOR "NSIS")
  else()
    message(STATUS "NSIS not found; only the portable ZIP package will be configured")
  endif()
endif()
if(WIN32 AND "NSIS" IN_LIST CPACK_GENERATOR)
  set(CPACK_NSIS_DISPLAY_NAME "FabGL Studio")
  set(CPACK_NSIS_PACKAGE_NAME "FabGL Studio")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
endif()
include(CPack)
