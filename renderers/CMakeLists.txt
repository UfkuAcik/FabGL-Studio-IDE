add_library(fabgl_renderers STATIC
  src/framebuffer.cpp
  src/renderer_2d.cpp
  src/raycast_renderer.cpp
  src/racer_renderer.cpp
  src/lowpoly_renderer.cpp)

target_include_directories(fabgl_renderers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fabgl_renderers PUBLIC fabgl_engine)
target_compile_features(fabgl_renderers PUBLIC cxx_std_20)
fgl_enable_warnings(fabgl_renderers)
fgl_enable_sanitizers(fabgl_renderers)

add_library(FabGLStudio::Renderers ALIAS fabgl_renderers)
