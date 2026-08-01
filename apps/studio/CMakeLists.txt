add_executable(fabgl_studio
  src/main.cpp
  src/BuildRunner.cpp
  src/BuildRunner.h
  src/CodeEditor.cpp
  src/CodeEditor.h
  src/EntityCommands.cpp
  src/EntityCommands.h
  src/EntityModel.cpp
  src/EntityModel.h
  src/EditorViews.cpp
  src/EditorViews.h
  src/MainWindow.cpp
  src/MainWindow.h
  src/ProjectDocument.cpp
  src/ProjectDocument.h
  src/SceneDocument.cpp
  src/SceneDocument.h)

add_executable(FabGLStudio::Studio ALIAS fabgl_studio)

target_compile_features(fabgl_studio PRIVATE cxx_std_20)
target_link_libraries(fabgl_studio PRIVATE
  fabgl_engine
  fabgl_renderers
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets)

set_target_properties(fabgl_studio PROPERTIES
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON
  OUTPUT_NAME "FabGLStudio"
  WIN32_EXECUTABLE ON)

if(COMMAND fgl_enable_warnings)
  fgl_enable_warnings(fabgl_studio)
endif()
if(COMMAND fgl_enable_sanitizers)
  fgl_enable_sanitizers(fabgl_studio)
endif()

install(TARGETS fabgl_studio
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT Studio)
