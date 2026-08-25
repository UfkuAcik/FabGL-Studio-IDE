add_library(fabgl_demo_runtime STATIC demo.cpp)
target_include_directories(fabgl_demo_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT TARGET fabgl_project_model)
  message(FATAL_ERROR "The PC player requires fabgl_project_model; enable FGL_BUILD_TOOLS")
endif()
target_link_libraries(fabgl_demo_runtime PUBLIC
  fabgl_renderers
  fabgl_engine
  fabgl_frameworks
  fabgl_project_model
  fabgl_project_runtime)
target_compile_features(fabgl_demo_runtime PUBLIC cxx_std_20)
fgl_enable_warnings(fabgl_demo_runtime)
fgl_enable_sanitizers(fabgl_demo_runtime)

add_executable(fabgl_player_pc main.cpp)

target_link_libraries(fabgl_player_pc PRIVATE fabgl_demo_runtime)
target_include_directories(fabgl_player_pc PRIVATE ${CMAKE_SOURCE_DIR}/tools/common)
target_compile_features(fabgl_player_pc PRIVATE cxx_std_20)
fgl_enable_warnings(fabgl_player_pc)
fgl_enable_sanitizers(fabgl_player_pc)

if(WIN32)
  target_sources(fabgl_player_pc PRIVATE win32_audio.cpp win32_audio.h win32_window.cpp)
  target_compile_definitions(fabgl_player_pc PRIVATE FGL_HAS_NATIVE_WINDOW=1 WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(fabgl_player_pc PRIVATE user32 gdi32 shell32 winmm)
else()
  target_compile_definitions(fabgl_player_pc PRIVATE FGL_HAS_NATIVE_WINDOW=0)
endif()

install(TARGETS fabgl_player_pc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
