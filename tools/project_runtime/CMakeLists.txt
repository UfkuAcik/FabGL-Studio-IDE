add_library(fabgl_project_runtime STATIC
  src/project_asset_library.cpp
  src/project_asset_streaming.cpp
  src/project_extension_modules.cpp
  src/project_extension_service_host.cpp
  src/project_scene_audio.cpp
  src/project_script_modules.cpp
  src/project_visual_host.cpp)

target_include_directories(fabgl_project_runtime PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(fabgl_project_runtime PUBLIC
  fabgl_project_model
  fabgl_asset_pipeline
  fabgl_renderers
  ${CMAKE_DL_LIBS})
target_compile_features(fabgl_project_runtime PUBLIC cxx_std_20)
fgl_enable_warnings(fabgl_project_runtime)
fgl_enable_sanitizers(fabgl_project_runtime)
