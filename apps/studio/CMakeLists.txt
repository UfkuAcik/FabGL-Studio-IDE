set(FGL_STUDIO_ASSET_BROWSER_SOURCES
  src/AssetBrowserController.cpp
  src/AssetBrowserController.h
  src/AssetBrowserImporters.cpp
  src/AssetBrowserImporters.h
  src/AssetBrowserModel.cpp)

add_library(fabgl_studio_asset_browser STATIC ${FGL_STUDIO_ASSET_BROWSER_SOURCES})
add_library(FabGLStudio::AssetBrowser ALIAS fabgl_studio_asset_browser)
target_compile_features(fabgl_studio_asset_browser PUBLIC cxx_std_20)
target_include_directories(fabgl_studio_asset_browser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fabgl_studio_asset_browser PUBLIC
  fabgl_asset_pipeline
  fabgl_project_model
  Qt6::Core
  Qt6::Gui)
set_target_properties(fabgl_studio_asset_browser PROPERTIES AUTOMOC ON)
if(WIN32)
  target_compile_definitions(fabgl_studio_asset_browser PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

set(FGL_STUDIO_SOURCES
  src/AdvancedEditorPanels.cpp
  src/AdvancedEditorPanels.h
  src/BuildRunner.cpp
  src/BuildRunner.h
  src/ClangdClient.cpp
  src/ClangdClient.h
  src/CodeEditor.cpp
  src/CodeEditor.h
  src/ComponentInspector.cpp
  src/ComponentInspector.h
  src/EntityCommands.cpp
  src/EntityCommands.h
  src/EntityModel.cpp
  src/EntityModel.h
  src/EditorViews.cpp
  src/EditorViews.h
  src/ExtensionServicePanel.cpp
  src/ExtensionServicePanel.h
  src/InputMapEditorWidget.cpp
  src/InputMapEditorWidget.h
  src/ImageImportSettingsWidget.cpp
  src/ImageImportSettingsWidget.h
  src/MainWindow.cpp
  src/MainWindow.h
  src/ProjectCreationDialog.cpp
  src/ProjectCreationDialog.h
  src/ProjectDocument.cpp
  src/ProjectDocument.h
  src/ProjectTrustStore.cpp
  src/ProjectTrustStore.h
  src/PreviewRestartController.h
  src/PrefabEditorPanel.cpp
  src/PrefabEditorPanel.h
  src/RecoveryManager.cpp
  src/RecoveryManager.h
  src/SceneDocument.cpp
  src/SceneDocument.h
  src/SerialConsoleWidget.cpp
  src/SerialConsoleWidget.h
  src/SpecialistEditorPanels.cpp
  src/SpecialistEditorPanels.h
  src/StudioPlaySession.cpp
  src/StudioPlaySession.h
  src/ToolchainSetupWidget.cpp
  src/ToolchainSetupWidget.h
  src/WorkflowCommands.cpp
  src/WorkflowCommands.h)

add_library(fabgl_studio_lib STATIC ${FGL_STUDIO_SOURCES})
add_library(FabGLStudio::Editor ALIAS fabgl_studio_lib)

target_compile_features(fabgl_studio_lib PUBLIC cxx_std_20)
target_include_directories(fabgl_studio_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fabgl_studio_lib PUBLIC
  fabgl_engine
  fabgl_frameworks
  fabgl_project_runtime
  fabgl_renderers
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets)
target_link_libraries(fabgl_studio_lib PRIVATE fabgl_studio_asset_browser)

target_compile_definitions(fabgl_studio_lib PUBLIC
  FABGL_STUDIO_VERSION="${PROJECT_VERSION}")

if(WIN32)
  target_sources(fabgl_studio_lib PRIVATE
    ../player_pc/win32_audio.cpp
    ../player_pc/win32_audio.h)
  target_include_directories(fabgl_studio_lib PRIVATE ../player_pc)
  target_compile_definitions(fabgl_studio_lib PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(fabgl_studio_lib PRIVATE winmm)
endif()

set_target_properties(fabgl_studio_lib PROPERTIES
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON)

add_executable(fabgl_studio src/main.cpp)
add_executable(FabGLStudio::Studio ALIAS fabgl_studio)

target_compile_features(fabgl_studio PRIVATE cxx_std_20)
target_link_libraries(fabgl_studio PRIVATE fabgl_studio_lib)

set_target_properties(fabgl_studio PROPERTIES
  OUTPUT_NAME "FabGLStudio"
  WIN32_EXECUTABLE ON)

foreach(studio_target IN ITEMS fabgl_studio_asset_browser fabgl_studio_lib fabgl_studio)
  if(COMMAND fgl_enable_warnings)
    fgl_enable_warnings(${studio_target})
  endif()
  if(COMMAND fgl_enable_sanitizers)
    fgl_enable_sanitizers(${studio_target})
  endif()
endforeach()

install(TARGETS fabgl_studio
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT Studio)
