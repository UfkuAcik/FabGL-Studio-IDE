add_library(fabgl_frameworks STATIC
  src/platformer.cpp
  src/top_down.cpp
  src/fps.cpp
  src/racer.cpp
  src/tps.cpp)

target_include_directories(fabgl_frameworks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fabgl_frameworks PUBLIC fabgl_engine)
target_compile_features(fabgl_frameworks PUBLIC cxx_std_20)
fgl_enable_warnings(fabgl_frameworks)
fgl_enable_sanitizers(fabgl_frameworks)

add_library(FabGLStudio::Frameworks ALIAS fabgl_frameworks)
