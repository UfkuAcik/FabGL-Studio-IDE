add_library(fabgl_frameworks STATIC
  src/platformer.cpp
  src/top_down.cpp
  src/fps.cpp
  src/racer.cpp
  src/scene_gameplay.cpp
  src/tps.cpp)

target_include_directories(fabgl_frameworks PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(fabgl_frameworks PUBLIC fabgl_engine)
target_compile_features(fabgl_frameworks PUBLIC cxx_std_20)
fgl_enable_warnings(fabgl_frameworks)
fgl_enable_sanitizers(fabgl_frameworks)

add_library(FabGLStudio::Frameworks ALIAS fabgl_frameworks)

set_target_properties(fabgl_frameworks PROPERTIES EXPORT_NAME Frameworks)
install(TARGETS fabgl_frameworks
        EXPORT FabGLStudioTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT SDK
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT SDK
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT SDK
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT SDK)
