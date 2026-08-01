cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FabGLEngineTests LANGUAGES CXX)
endif()

enable_testing()

if(NOT TARGET fabgl_engine)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../engine
        ${CMAKE_CURRENT_BINARY_DIR}/engine
    )
endif()

add_executable(fabgl_engine_tests
    core_tests.cpp
    physics_animation_tests.cpp
    prefab_visual_save_tests.cpp
    resource_tests.cpp
    runtime_loop_input_tests.cpp
    runtime_systems_tests.cpp
    scene_tests.cpp
    scripting_tests.cpp
    test_main.cpp
    ui_package_component_tests.cpp
)

target_link_libraries(fabgl_engine_tests PRIVATE fabgl_engine)
target_compile_features(fabgl_engine_tests PRIVATE cxx_std_20)
target_include_directories(fabgl_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(COMMAND fgl_enable_warnings)
    fgl_enable_warnings(fabgl_engine_tests)
elseif(MSVC)
    target_compile_options(fabgl_engine_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(fabgl_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()


if(COMMAND fgl_enable_sanitizers)
    fgl_enable_sanitizers(fabgl_engine_tests)
endif()

add_test(NAME fabgl_engine_tests COMMAND fabgl_engine_tests)

function(fgl_add_test_executable target)
    add_executable(${target} ${ARGN} test_main.cpp)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(COMMAND fgl_enable_warnings)
        fgl_enable_warnings(${target})
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(COMMAND fgl_enable_sanitizers)
        fgl_enable_sanitizers(${target})
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

if(TARGET fabgl_demo_runtime)
    fgl_add_test_executable(fabgl_rendering_tests rendering_tests.cpp)
    target_link_libraries(fabgl_rendering_tests PRIVATE fabgl_demo_runtime fabgl_renderers)
endif()

if(TARGET fabgl_asset_pipeline AND TARGET fabgl_project_model)
    fgl_add_test_executable(fabgl_asset_pipeline_tests asset_pipeline_tests.cpp project_tests.cpp)
    target_link_libraries(fabgl_asset_pipeline_tests PRIVATE fabgl_asset_pipeline fabgl_project_model)
endif()

if(TARGET fabgl_frameworks)
    fgl_add_test_executable(fabgl_framework_tests framework_tests.cpp)
    target_link_libraries(fabgl_framework_tests PRIVATE fabgl_frameworks)
endif()

if(TARGET fabgl_project_cli AND TARGET fabgl_player_pc)
    add_test(
        NAME fabgl_examples_integration
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_CLI=$<TARGET_FILE:fabgl_project_cli>
            -DPLAYER=$<TARGET_FILE:fabgl_player_pc>
            -DREPOSITORY_ROOT=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_examples.cmake)
    set_tests_properties(fabgl_examples_integration PROPERTIES TIMEOUT 60)
endif()
