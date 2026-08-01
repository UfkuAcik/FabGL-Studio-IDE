cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FabGLEngine LANGUAGES CXX)
endif()

add_library(fabgl_engine STATIC
    src/animation.cpp
    src/audio_mixer.cpp
    src/builtin_components.cpp
    src/component.cpp
    src/engine_loop.cpp
    src/entity.cpp
    src/grid_navigation.cpp
    src/guid.cpp
    src/input_map.cpp
    src/logger.cpp
    src/package_manifest.cpp
    src/particle_system.cpp
    src/physics2d.cpp
    src/prefab.cpp
    src/profiler.cpp
    src/reflection.cpp
    src/resource_cache.cpp
    src/save_system.cpp
    src/script_component.cpp
    src/scene.cpp
    src/scene_serializer.cpp
    src/transform_component.cpp
    src/ui_layout.cpp
    src/visual_graph.cpp
)

target_include_directories(fabgl_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(fabgl_engine PUBLIC cxx_std_20)

if(COMMAND fgl_enable_warnings)
    fgl_enable_warnings(fabgl_engine)
elseif(MSVC)
    target_compile_options(fabgl_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(fabgl_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(COMMAND fgl_enable_sanitizers)
    fgl_enable_sanitizers(fabgl_engine)
endif()

add_library(FabGLStudio::Engine ALIAS fabgl_engine)
