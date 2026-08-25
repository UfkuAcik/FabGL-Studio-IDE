cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED ENV{FGL_QOFFSCREEN_PLUGIN} OR
   "$ENV{FGL_QOFFSCREEN_PLUGIN}" STREQUAL "")
  message(FATAL_ERROR "FGL_QOFFSCREEN_PLUGIN must name the locked Qt offscreen plugin")
endif()
file(TO_CMAKE_PATH "$ENV{FGL_QOFFSCREEN_PLUGIN}" qoffscreen_plugin)
if(NOT EXISTS "${qoffscreen_plugin}")
  message(FATAL_ERROR "Locked Qt offscreen plugin is missing: ${qoffscreen_plugin}")
endif()
if(NOT DEFINED CMAKE_INSTALL_PREFIX OR CMAKE_INSTALL_PREFIX STREQUAL "")
  message(FATAL_ERROR "CPack did not provide an installation prefix")
endif()

file(INSTALL
  DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/platforms"
  TYPE FILE
  FILES "${qoffscreen_plugin}")
