cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FabGLEngine LANGUAGES CXX)
endif()

add_library(fabgl_engine STATIC
    src/ai_behaviors.cpp
    src/animation.cpp
    src/animation_authoring.cpp
    src/asset_streaming_manager.cpp
    src/audio_mixer.cpp
    src/builtin_components.cpp
    src/component.cpp
    src/engine_loop.cpp
    src/entity.cpp
    src/extension_registry.cpp
    src/grid_navigation.cpp
    src/guid.cpp
    src/input_map.cpp
    src/logger.cpp
    src/material.cpp
    src/material_serializer.cpp
    src/package_manifest.cpp
    src/particle_system.cpp
    src/physics2d.cpp
    src/physics3d.cpp
    src/prefab.cpp
    src/prefab_instance_serializer.cpp
    src/prefab_serializer.cpp
    src/profiler.cpp
    src/reflection.cpp
    src/resource_cache.cpp
    src/runtime_widgets.cpp
    src/save_system.cpp
    src/scene_save_state.cpp
    src/scene_runtime.cpp
  src/script_component.cpp
  src/script_module.cpp
    src/scene.cpp
    src/scene_serializer.cpp
    src/transform_component.cpp
    src/ui_layout.cpp
    src/visual_graph.cpp
    src/visual_graph_format.cpp
)

target_include_directories(fabgl_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../platforms/common
)

target_compile_features(fabgl_engine PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(fabgl_engine PRIVATE bcrypt)
endif()

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

set_target_properties(fabgl_engine PROPERTIES EXPORT_NAME Engine)
install(TARGETS fabgl_engine
        EXPORT FabGLStudioTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT SDK
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT SDK
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT SDK
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT SDK)
