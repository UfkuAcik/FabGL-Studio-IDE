if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/toolchain_manager/CMakeLists.txt")
  add_subdirectory(toolchain_manager)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asset_pipeline/CMakeLists.txt")
  add_subdirectory(asset_pipeline)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/project_cli/CMakeLists.txt")
  add_subdirectory(project_cli)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/project_runtime/CMakeLists.txt")
  add_subdirectory(project_runtime)
endif()
