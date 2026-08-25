add_library(fabgl_renderers STATIC
  src/framebuffer.cpp
  src/renderer_2d.cpp
  src/raycast_renderer.cpp
  src/raycast_map_asset.cpp
  src/racer_renderer.cpp
  src/racer_track.cpp
  src/scene_presenter.cpp
  src/lowpoly_renderer.cpp)

target_include_directories(fabgl_renderers PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(fabgl_renderers PUBLIC fabgl_engine)
target_compile_features(fabgl_renderers PUBLIC cxx_std_20)
fgl_enable_warnings(fabgl_renderers)
fgl_enable_sanitizers(fabgl_renderers)

add_library(FabGLStudio::Renderers ALIAS fabgl_renderers)

set_target_properties(fabgl_renderers PROPERTIES EXPORT_NAME Renderers)
install(TARGETS fabgl_renderers
        EXPORT FabGLStudioTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT SDK
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT SDK
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT SDK
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT SDK)
