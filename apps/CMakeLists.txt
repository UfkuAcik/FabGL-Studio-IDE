if(FGL_BUILD_PLAYER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/player_pc/CMakeLists.txt")
  add_subdirectory(player_pc)
endif()

if(FGL_BUILD_STUDIO)
  find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)

  if(Qt6_FOUND)
    message(STATUS "Qt 6 found; enabling the FabGL Studio desktop editor")
    add_subdirectory(studio)
  else()
    message(WARNING
      "FabGL Studio editor skipped: Qt 6 Core, Gui, and Widgets were not found. "
      "The portable engine, command-line tools, and tests can still be configured. "
      "Set CMAKE_PREFIX_PATH to a compiler-compatible Qt 6 installation to enable Studio.")
  endif()
endif()
