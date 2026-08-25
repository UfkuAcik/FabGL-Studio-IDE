cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FabGLEngineTests LANGUAGES CXX)
endif()

enable_testing()

if(WIN32)
    find_program(FGL_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(FGL_POWERSHELL_EXECUTABLE)
        add_test(
            NAME fabgl_serial_port_detection_tests
            COMMAND ${FGL_POWERSHELL_EXECUTABLE}
                -NoLogo -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/hardware/test_port_detection.ps1)
        set_tests_properties(fabgl_serial_port_detection_tests PROPERTIES TIMEOUT 30)
        add_test(
            NAME fabgl_esp32_build_profile_tests
            COMMAND ${FGL_POWERSHELL_EXECUTABLE}
                -NoLogo -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/hardware/test_esp32_build_profiles.ps1)
        set_tests_properties(fabgl_esp32_build_profile_tests PROPERTIES TIMEOUT 30)
        add_test(
            NAME fabgl_project_build_pipeline_tests
            COMMAND ${FGL_POWERSHELL_EXECUTABLE}
                -NoLogo -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/test_project_build_pipeline.ps1)
        set_tests_properties(fabgl_project_build_pipeline_tests PROPERTIES TIMEOUT 30)
        add_test(
            NAME fabgl_hardware_diagnostic_contract_tests
            COMMAND ${FGL_POWERSHELL_EXECUTABLE}
                -NoLogo -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/hardware/test_hardware_diagnostics.ps1)
        set_tests_properties(fabgl_hardware_diagnostic_contract_tests PROPERTIES TIMEOUT 30)
        add_test(
            NAME fabgl_nsis_bootstrap_contract_tests
            COMMAND ${FGL_POWERSHELL_EXECUTABLE}
                -NoLogo -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/scripts/bootstrap_nsis.ps1 -DryRun)
        set_tests_properties(fabgl_nsis_bootstrap_contract_tests PROPERTIES TIMEOUT 30)
        add_test(
            NAME fabgl_source_package_contract_tests
            COMMAND ${FGL_POWERSHELL_EXECUTABLE}
                -NoLogo -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/test_source_package.ps1)
        set_tests_properties(fabgl_source_package_contract_tests PROPERTIES TIMEOUT 60)
    endif()
endif()

if(NOT TARGET fabgl_engine)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../engine
        ${CMAKE_CURRENT_BINARY_DIR}/engine
    )
endif()

add_executable(fabgl_engine_tests
    animation_authoring_tests.cpp
    asset_streaming_manager_tests.cpp
    core_tests.cpp
    extension_registry_tests.cpp
    material_serialization_tests.cpp
    material_tests.cpp
    physics_animation_tests.cpp
    prefab_serialization_tests.cpp
    prefab_visual_save_tests.cpp
    resource_tests.cpp
    runtime_feature_tests.cpp
    runtime_loop_input_tests.cpp
    runtime_systems_tests.cpp
    scene_save_state_tests.cpp
    scene_runtime_tests.cpp
    scene_tests.cpp
    scripting_tests.cpp
    test_main.cpp
    ui_package_component_tests.cpp
    visual_graph_v1_tests.cpp
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
if(WIN32 AND MINGW)
    get_filename_component(FGL_TEST_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_property(TEST fabgl_engine_tests APPEND PROPERTY
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FGL_TEST_MINGW_BIN}")
endif()

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
    if(WIN32 AND MINGW)
        get_filename_component(FGL_TEST_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_property(TEST ${target} APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FGL_TEST_MINGW_BIN}")
    endif()
endfunction()

if(TARGET fabgl_demo_runtime)
    fgl_add_test_executable(fabgl_rendering_tests rendering_tests.cpp raycast_map_asset_tests.cpp
        scene_presentation_tests.cpp project_asset_streaming_tests.cpp)
    target_link_libraries(fabgl_rendering_tests PRIVATE fabgl_demo_runtime fabgl_renderers)
    target_compile_definitions(fabgl_rendering_tests PRIVATE
        FGL_TEST_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}")
endif()

if(TARGET fabgl_renderers)
    fgl_add_test_executable(fabgl_performance_regression_tests performance_regression_tests.cpp)
    target_link_libraries(fabgl_performance_regression_tests PRIVATE fabgl_renderers)
    set_tests_properties(fabgl_performance_regression_tests PROPERTIES
        LABELS performance
        TIMEOUT 90)
endif()

if(TARGET fabgl_asset_pipeline AND TARGET fabgl_project_model)
    fgl_add_test_executable(fabgl_asset_pipeline_tests
        asset_pipeline_tests.cpp
        project_tests.cpp
        performance_budget_tests.cpp
        esp32_capability_tests.cpp
        esp32_runtime_tests.cpp)
    target_link_libraries(fabgl_asset_pipeline_tests PRIVATE fabgl_asset_pipeline fabgl_project_model)
    target_include_directories(fabgl_asset_pipeline_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/platforms/fabgl/firmware)
    if(TARGET fabgl_renderers)
        target_link_libraries(fabgl_asset_pipeline_tests PRIVATE fabgl_renderers)
    endif()
endif()

if(TARGET fabgl_frameworks)
    fgl_add_test_executable(fabgl_framework_tests framework_tests.cpp scene_gameplay_tests.cpp)
    target_link_libraries(fabgl_framework_tests PRIVATE fabgl_frameworks)
endif()

if(TARGET fabgl_project_cli)
    add_test(
        NAME fabgl_cross_process_guid_tests
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_CLI=$<TARGET_FILE:fabgl_project_cli>
            -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/guid-process-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_guid_process_uniqueness.cmake)
    set_tests_properties(fabgl_cross_process_guid_tests PROPERTIES TIMEOUT 30)
    if(WIN32 AND MINGW)
        get_filename_component(FGL_TEST_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_property(TEST fabgl_cross_process_guid_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FGL_TEST_MINGW_BIN}")
    endif()
    add_test(
        NAME fabgl_local_package_cli_tests
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_CLI=$<TARGET_FILE:fabgl_project_cli>
            -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/local-package-cli-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_local_package_cli.cmake)
    set_tests_properties(fabgl_local_package_cli_tests PROPERTIES TIMEOUT 30)
    if(WIN32 AND MINGW)
        get_filename_component(FGL_TEST_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_property(TEST fabgl_local_package_cli_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FGL_TEST_MINGW_BIN}")
    endif()
endif()

if(TARGET fabgl_project_runtime)
    fgl_add_test_executable(fabgl_project_visual_host_tests project_visual_host_tests.cpp)
    target_link_libraries(fabgl_project_visual_host_tests PRIVATE fabgl_project_runtime)

    add_library(fabgl_extension_test_module SHARED
        fixtures/extension_module.cpp)
    target_link_libraries(fabgl_extension_test_module PRIVATE fabgl_engine)
    target_include_directories(fabgl_extension_test_module PRIVATE
        ${CMAKE_SOURCE_DIR}/tools/project_runtime/include)
    target_compile_features(fabgl_extension_test_module PRIVATE cxx_std_20)
    set_target_properties(fabgl_extension_test_module PROPERTIES
        PREFIX ""
        OUTPUT_NAME "fabgl_extension_test_module")
    fgl_enable_warnings(fabgl_extension_test_module)

    add_executable(fabgl_project_extension_module_tests
        project_extension_module_tests.cpp)
    target_link_libraries(fabgl_project_extension_module_tests PRIVATE fabgl_project_runtime)
    target_compile_features(fabgl_project_extension_module_tests PRIVATE cxx_std_20)
    target_include_directories(fabgl_project_extension_module_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    fgl_enable_warnings(fabgl_project_extension_module_tests)
    add_dependencies(fabgl_project_extension_module_tests fabgl_extension_test_module)
    add_test(NAME fabgl_project_extension_module_tests
        COMMAND fabgl_project_extension_module_tests
            $<TARGET_FILE:fabgl_extension_test_module>)
    if(WIN32 AND MINGW)
        get_filename_component(FGL_EXTENSION_TEST_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_property(TEST fabgl_project_extension_module_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FGL_EXTENSION_TEST_MINGW_BIN}")
    endif()

    add_library(fabgl_native_script_test_module SHARED
        fixtures/native_script_module.cpp)
    target_link_libraries(fabgl_native_script_test_module PRIVATE fabgl_engine)
    target_compile_features(fabgl_native_script_test_module PRIVATE cxx_std_20)
    set_target_properties(fabgl_native_script_test_module PROPERTIES
        PREFIX ""
        OUTPUT_NAME "fabgl_native_script_test_module")
    fgl_enable_warnings(fabgl_native_script_test_module)

    add_executable(fabgl_native_script_module_tests native_script_module_tests.cpp)
    target_link_libraries(fabgl_native_script_module_tests PRIVATE fabgl_project_runtime)
    target_compile_features(fabgl_native_script_module_tests PRIVATE cxx_std_20)
    fgl_enable_warnings(fabgl_native_script_module_tests)
    add_test(NAME fabgl_native_script_module_tests
        COMMAND fabgl_native_script_module_tests
            $<TARGET_FILE:fabgl_native_script_test_module>)
    if(WIN32 AND MINGW)
        get_filename_component(FGL_NATIVE_SCRIPT_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_property(TEST fabgl_native_script_module_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FGL_NATIVE_SCRIPT_MINGW_BIN}")
    endif()
endif()

if(TARGET fabgl_project_cli AND TARGET fabgl_player_pc)
    if(TARGET fabgl_extension_test_module)
        add_test(
            NAME fabgl_player_extension_integration
            COMMAND ${CMAKE_COMMAND}
                -DPROJECT_CLI=$<TARGET_FILE:fabgl_project_cli>
                -DPLAYER=$<TARGET_FILE:fabgl_player_pc>
                -DEXTENSION_MODULE=$<TARGET_FILE:fabgl_extension_test_module>
                -DREPOSITORY_ROOT=${CMAKE_SOURCE_DIR}
                -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/player-extension-integration
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_player_extensions.cmake)
        set_tests_properties(fabgl_player_extension_integration PROPERTIES TIMEOUT 60)
        if(WIN32 AND MINGW)
            get_filename_component(FGL_PLAYER_EXTENSION_MINGW_BIN
                "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set_property(TEST fabgl_player_extension_integration APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_PLAYER_EXTENSION_MINGW_BIN}")
        endif()
    endif()

    add_test(
        NAME fabgl_project_prepare_integration
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_CLI=$<TARGET_FILE:fabgl_project_cli>
            -DPLAYER=$<TARGET_FILE:fabgl_player_pc>
            -DREPOSITORY_ROOT=${CMAKE_SOURCE_DIR}
            -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/project-prepare-integration
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_project_prepare.cmake)
    set_tests_properties(fabgl_project_prepare_integration PROPERTIES TIMEOUT 60)
    if(WIN32 AND MINGW)
        get_filename_component(FGL_PREPARE_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_property(TEST fabgl_project_prepare_integration APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FGL_PREPARE_MINGW_BIN}")
    endif()

    add_test(
        NAME fabgl_examples_integration
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_CLI=$<TARGET_FILE:fabgl_project_cli>
            -DPLAYER=$<TARGET_FILE:fabgl_player_pc>
            -DREPOSITORY_ROOT=${CMAKE_SOURCE_DIR}
            -DTEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/platformer-recording.fglreplay
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_examples.cmake)
    set_tests_properties(fabgl_examples_integration PROPERTIES TIMEOUT 60)
    if(WIN32 AND MINGW)
        get_filename_component(FGL_TEST_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_property(TEST fabgl_examples_integration APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FGL_TEST_MINGW_BIN}")
    endif()
endif()

if(TARGET fabgl_studio_lib)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

    add_executable(fabgl_preview_restart_controller_tests
        studio/preview_restart_controller_tests.cpp
    )
    target_compile_features(fabgl_preview_restart_controller_tests PRIVATE cxx_std_20)
    target_include_directories(fabgl_preview_restart_controller_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/studio/src
    )
    target_link_libraries(fabgl_preview_restart_controller_tests PRIVATE
        Qt6::Core
        Qt6::Test
    )
    set_target_properties(fabgl_preview_restart_controller_tests PROPERTIES AUTOMOC ON)
    if(COMMAND fgl_enable_warnings)
        fgl_enable_warnings(fabgl_preview_restart_controller_tests)
    endif()
    if(COMMAND fgl_enable_sanitizers)
        fgl_enable_sanitizers(fabgl_preview_restart_controller_tests)
    endif()
    add_test(NAME fabgl_preview_restart_controller_tests
        COMMAND fabgl_preview_restart_controller_tests
    )
    set_tests_properties(fabgl_preview_restart_controller_tests PROPERTIES TIMEOUT 30)
    if(WIN32)
        set_property(TEST fabgl_preview_restart_controller_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        if(MINGW)
            get_filename_component(FGL_PREVIEW_RESTART_MINGW_BIN
                "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set_property(TEST fabgl_preview_restart_controller_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_PREVIEW_RESTART_MINGW_BIN}"
            )
        endif()
    endif()

    add_executable(fabgl_extension_service_panel_tests
        studio/extension_service_panel_tests.cpp
    )
    target_compile_features(fabgl_extension_service_panel_tests PRIVATE cxx_std_20)
    target_link_libraries(fabgl_extension_service_panel_tests PRIVATE
        fabgl_studio_lib
        Qt6::Test
    )
    set_target_properties(fabgl_extension_service_panel_tests PROPERTIES AUTOMOC ON)
    if(COMMAND fgl_enable_warnings)
        fgl_enable_warnings(fabgl_extension_service_panel_tests)
    endif()
    if(COMMAND fgl_enable_sanitizers)
        fgl_enable_sanitizers(fabgl_extension_service_panel_tests)
    endif()
    add_test(NAME fabgl_extension_service_panel_tests
        COMMAND ${CMAKE_COMMAND}
            "-DTEST_EXECUTABLE=$<TARGET_FILE:fabgl_extension_service_panel_tests>"
            "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:fabgl_extension_service_panel_tests>"
            "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/fabgl_extension_service_panel_tests.log"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/studio/run_qtest.cmake
    )
    set_tests_properties(fabgl_extension_service_panel_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60
    )
    if(WIN32)
        set_property(TEST fabgl_extension_service_panel_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        if(MINGW)
            get_filename_component(FGL_EXTENSION_PANEL_MINGW_BIN
                "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set_property(TEST fabgl_extension_service_panel_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_EXTENSION_PANEL_MINGW_BIN}"
            )
        endif()
    endif()

    if(TARGET fabgl_studio_asset_browser)
        add_executable(fabgl_asset_browser_controller_tests
            studio/asset_browser_controller_tests.cpp
        )
        target_compile_features(fabgl_asset_browser_controller_tests PRIVATE cxx_std_20)
        target_link_libraries(fabgl_asset_browser_controller_tests PRIVATE
            fabgl_studio_asset_browser
            Qt6::Test
        )
        set_target_properties(fabgl_asset_browser_controller_tests PROPERTIES AUTOMOC ON)
        if(COMMAND fgl_enable_warnings)
            fgl_enable_warnings(fabgl_asset_browser_controller_tests)
        endif()
        if(COMMAND fgl_enable_sanitizers)
            fgl_enable_sanitizers(fabgl_asset_browser_controller_tests)
        endif()
        add_test(NAME fabgl_asset_browser_controller_tests
            COMMAND ${CMAKE_COMMAND}
                "-DTEST_EXECUTABLE=$<TARGET_FILE:fabgl_asset_browser_controller_tests>"
                "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:fabgl_asset_browser_controller_tests>"
                "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/fabgl_asset_browser_controller_tests.log"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/studio/run_qtest.cmake
        )
        set_tests_properties(fabgl_asset_browser_controller_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 60
        )
        if(WIN32)
            set_property(TEST fabgl_asset_browser_controller_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            )
            if(MINGW)
                get_filename_component(FGL_ASSET_BROWSER_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
                set_property(TEST fabgl_asset_browser_controller_tests APPEND PROPERTY
                    ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:${FGL_ASSET_BROWSER_MINGW_BIN}"
                )
            endif()
        endif()
    endif()

    if(TARGET fabgl_native_script_test_module)
        add_executable(fabgl_studio_play_session_tests
            studio/studio_play_session_tests.cpp
        )
        target_compile_features(fabgl_studio_play_session_tests PRIVATE cxx_std_20)
        target_link_libraries(fabgl_studio_play_session_tests PRIVATE fabgl_studio_lib)
        add_dependencies(fabgl_studio_play_session_tests fabgl_native_script_test_module)
        if(COMMAND fgl_enable_warnings)
            fgl_enable_warnings(fabgl_studio_play_session_tests)
        endif()
        if(COMMAND fgl_enable_sanitizers)
            fgl_enable_sanitizers(fabgl_studio_play_session_tests)
        endif()
        add_test(NAME fabgl_studio_play_session_tests
            COMMAND fabgl_studio_play_session_tests
                ${CMAKE_SOURCE_DIR}
                $<TARGET_FILE:fabgl_native_script_test_module>
        )
        set_tests_properties(fabgl_studio_play_session_tests PROPERTIES TIMEOUT 60)
        if(WIN32)
            set_property(TEST fabgl_studio_play_session_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            )
            if(MINGW)
                get_filename_component(FGL_STUDIO_PLAY_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
                set_property(TEST fabgl_studio_play_session_tests APPEND PROPERTY
                    ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:${FGL_STUDIO_PLAY_MINGW_BIN}"
                )
            endif()
        endif()
    endif()

    add_executable(fabgl_fake_lsp_server
        studio/fake_lsp_server.cpp
    )
    target_compile_features(fabgl_fake_lsp_server PRIVATE cxx_std_20)
    target_link_libraries(fabgl_fake_lsp_server PRIVATE Qt6::Core)

    add_executable(fabgl_clangd_client_tests
        studio/clangd_client_tests.cpp
        ${PROJECT_SOURCE_DIR}/apps/studio/src/ClangdClient.cpp
        ${PROJECT_SOURCE_DIR}/apps/studio/src/ClangdClient.h
    )
    target_compile_features(fabgl_clangd_client_tests PRIVATE cxx_std_20)
    target_include_directories(fabgl_clangd_client_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/studio/src
    )
    target_link_libraries(fabgl_clangd_client_tests PRIVATE Qt6::Core Qt6::Test)
    add_dependencies(fabgl_clangd_client_tests fabgl_fake_lsp_server)
    set_target_properties(fabgl_clangd_client_tests PROPERTIES AUTOMOC ON)
    foreach(clangd_test_target IN ITEMS fabgl_fake_lsp_server fabgl_clangd_client_tests)
        if(COMMAND fgl_enable_warnings)
            fgl_enable_warnings(${clangd_test_target})
        endif()
        if(COMMAND fgl_enable_sanitizers)
            fgl_enable_sanitizers(${clangd_test_target})
        endif()
    endforeach()

    add_test(NAME fabgl_clangd_client_tests
        COMMAND ${CMAKE_COMMAND}
            "-DTEST_EXECUTABLE=$<TARGET_FILE:fabgl_clangd_client_tests>"
            "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:fabgl_clangd_client_tests>"
            "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/fabgl_clangd_client_tests.log"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/studio/run_qtest.cmake
    )
    set_tests_properties(fabgl_clangd_client_tests PROPERTIES TIMEOUT 60)
    if(WIN32)
        set_property(TEST fabgl_clangd_client_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        if(MINGW)
            get_filename_component(FGL_CLANGD_TEST_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set_property(TEST fabgl_clangd_client_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_CLANGD_TEST_MINGW_BIN}"
            )
        endif()
    endif()

    add_executable(fabgl_code_editor_security_tests
        studio/code_editor_security_tests.cpp
        ${PROJECT_SOURCE_DIR}/apps/studio/src/ClangdClient.cpp
        ${PROJECT_SOURCE_DIR}/apps/studio/src/ClangdClient.h
        ${PROJECT_SOURCE_DIR}/apps/studio/src/CodeEditor.cpp
        ${PROJECT_SOURCE_DIR}/apps/studio/src/CodeEditor.h
    )
    target_compile_features(fabgl_code_editor_security_tests PRIVATE cxx_std_20)
    target_include_directories(fabgl_code_editor_security_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/studio/src
    )
    target_link_libraries(fabgl_code_editor_security_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
    )
    set_target_properties(fabgl_code_editor_security_tests PROPERTIES AUTOMOC ON)
    if(COMMAND fgl_enable_warnings)
        fgl_enable_warnings(fabgl_code_editor_security_tests)
    endif()
    if(COMMAND fgl_enable_sanitizers)
        fgl_enable_sanitizers(fabgl_code_editor_security_tests)
    endif()
    add_test(NAME fabgl_code_editor_security_tests
        COMMAND ${CMAKE_COMMAND}
            "-DTEST_EXECUTABLE=$<TARGET_FILE:fabgl_code_editor_security_tests>"
            "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:fabgl_code_editor_security_tests>"
            "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/fabgl_code_editor_security_tests.log"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/studio/run_qtest.cmake
    )
    set_tests_properties(fabgl_code_editor_security_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60
    )
    if(WIN32)
        set_property(TEST fabgl_code_editor_security_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        if(MINGW)
            set_property(TEST fabgl_code_editor_security_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_CLANGD_TEST_MINGW_BIN}"
            )
        endif()
    endif()

    add_executable(fabgl_studio_smoke_tests
        studio/studio_smoke_tests.cpp
    )
    target_compile_features(fabgl_studio_smoke_tests PRIVATE cxx_std_20)
    target_compile_definitions(fabgl_studio_smoke_tests PRIVATE
        FGL_TEST_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
    )
    if(TARGET fabgl_native_script_test_module)
        add_dependencies(fabgl_studio_smoke_tests fabgl_native_script_test_module)
        target_compile_definitions(fabgl_studio_smoke_tests PRIVATE
            FGL_TEST_NATIVE_SCRIPT_MODULE="$<TARGET_FILE:fabgl_native_script_test_module>"
        )
    endif()
    target_link_libraries(fabgl_studio_smoke_tests PRIVATE
        fabgl_studio_lib
        Qt6::Test
    )
    set_target_properties(fabgl_studio_smoke_tests PROPERTIES AUTOMOC ON)
    if(COMMAND fgl_enable_warnings)
        fgl_enable_warnings(fabgl_studio_smoke_tests)
    endif()
    if(COMMAND fgl_enable_sanitizers)
        fgl_enable_sanitizers(fabgl_studio_smoke_tests)
    endif()

    add_test(NAME fabgl_studio_smoke_tests
        COMMAND ${CMAKE_COMMAND}
            "-DTEST_EXECUTABLE=$<TARGET_FILE:fabgl_studio_smoke_tests>"
            "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:fabgl_studio_smoke_tests>"
            "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/fabgl_studio_smoke_tests.log"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/studio/run_qtest.cmake
    )
    set_tests_properties(fabgl_studio_smoke_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
    )
    if(WIN32)
        set_property(TEST fabgl_studio_smoke_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        if(MINGW)
            get_filename_component(FGL_STUDIO_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set_property(TEST fabgl_studio_smoke_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_STUDIO_MINGW_BIN}"
            )
        endif()
    endif()

    add_executable(fabgl_prefab_editor_tests
        studio/prefab_editor_tests.cpp
    )
    target_compile_features(fabgl_prefab_editor_tests PRIVATE cxx_std_20)
    target_include_directories(fabgl_prefab_editor_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/studio/src
    )
    target_link_libraries(fabgl_prefab_editor_tests PRIVATE
        fabgl_studio_lib
        Qt6::Test
    )
    target_compile_definitions(fabgl_prefab_editor_tests PRIVATE
        FGL_TEST_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
    )
    set_target_properties(fabgl_prefab_editor_tests PROPERTIES AUTOMOC ON)
    if(COMMAND fgl_enable_warnings)
        fgl_enable_warnings(fabgl_prefab_editor_tests)
    endif()
    if(COMMAND fgl_enable_sanitizers)
        fgl_enable_sanitizers(fabgl_prefab_editor_tests)
    endif()

    add_test(NAME fabgl_prefab_editor_tests
        COMMAND ${CMAKE_COMMAND}
            "-DTEST_EXECUTABLE=$<TARGET_FILE:fabgl_prefab_editor_tests>"
            "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:fabgl_prefab_editor_tests>"
            "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/fabgl_prefab_editor_tests.log"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/studio/run_qtest.cmake
    )
    set_tests_properties(fabgl_prefab_editor_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
    )
    if(WIN32)
        set_property(TEST fabgl_prefab_editor_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        if(MINGW)
            get_filename_component(FGL_PREFAB_EDITOR_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set_property(TEST fabgl_prefab_editor_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_PREFAB_EDITOR_MINGW_BIN}"
            )
        endif()
    endif()

    add_executable(fabgl_project_wizard_input_tests
        studio/project_wizard_input_tests.cpp
    )
    target_compile_features(fabgl_project_wizard_input_tests PRIVATE cxx_std_20)
    target_include_directories(fabgl_project_wizard_input_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/studio/src
    )
    target_link_libraries(fabgl_project_wizard_input_tests PRIVATE
        fabgl_studio_lib
        Qt6::Test
    )
    set_target_properties(fabgl_project_wizard_input_tests PROPERTIES AUTOMOC ON)
    if(COMMAND fgl_enable_warnings)
        fgl_enable_warnings(fabgl_project_wizard_input_tests)
    endif()
    if(COMMAND fgl_enable_sanitizers)
        fgl_enable_sanitizers(fabgl_project_wizard_input_tests)
    endif()
    add_test(NAME fabgl_project_wizard_input_tests
        COMMAND ${CMAKE_COMMAND}
            "-DTEST_EXECUTABLE=$<TARGET_FILE:fabgl_project_wizard_input_tests>"
            "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:fabgl_project_wizard_input_tests>"
            "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/fabgl_project_wizard_input_tests.log"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/studio/run_qtest.cmake
    )
    set_tests_properties(fabgl_project_wizard_input_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180
    )
    if(WIN32)
        set_property(TEST fabgl_project_wizard_input_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        if(MINGW)
            get_filename_component(FGL_PROJECT_WIZARD_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set_property(TEST fabgl_project_wizard_input_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_PROJECT_WIZARD_MINGW_BIN}"
            )
        endif()
    endif()

    add_executable(fabgl_specialist_editor_tests
        studio/specialist_editor_tests.cpp
        ${PROJECT_SOURCE_DIR}/apps/studio/src/SpecialistEditorPanels.cpp
        ${PROJECT_SOURCE_DIR}/apps/studio/src/SpecialistEditorPanels.h
    )
    target_compile_features(fabgl_specialist_editor_tests PRIVATE cxx_std_20)
    target_include_directories(fabgl_specialist_editor_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/studio/src
    )
    target_link_libraries(fabgl_specialist_editor_tests PRIVATE
        fabgl_engine
        fabgl_asset_pipeline
        fabgl_renderers
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
    )
    set_target_properties(fabgl_specialist_editor_tests PROPERTIES AUTOMOC ON)
    if(COMMAND fgl_enable_warnings)
        fgl_enable_warnings(fabgl_specialist_editor_tests)
    endif()
    if(COMMAND fgl_enable_sanitizers)
        fgl_enable_sanitizers(fabgl_specialist_editor_tests)
    endif()

    add_test(NAME fabgl_specialist_editor_tests
        COMMAND ${CMAKE_COMMAND}
            "-DTEST_EXECUTABLE=$<TARGET_FILE:fabgl_specialist_editor_tests>"
            "-DTEST_WORKING_DIRECTORY=$<TARGET_FILE_DIR:fabgl_specialist_editor_tests>"
            "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/fabgl_specialist_editor_tests.log"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/studio/run_qtest.cmake
    )
    set_tests_properties(fabgl_specialist_editor_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
    )
    if(WIN32)
        set_property(TEST fabgl_specialist_editor_tests APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        if(MINGW)
            set_property(TEST fabgl_specialist_editor_tests APPEND PROPERTY
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${FGL_STUDIO_MINGW_BIN}"
            )
        endif()
    endif()
endif()
