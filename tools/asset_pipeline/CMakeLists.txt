add_library(fabgl_asset_pipeline STATIC
  src/asset_database.cpp
  src/asset_pack.cpp
  src/audio_importer.cpp
  src/image_pipeline.cpp
  src/file_io.cpp)

if(WIN32)
  target_sources(fabgl_asset_pipeline PRIVATE src/image_loader_wic.cpp)
  target_link_libraries(fabgl_asset_pipeline PRIVATE windowscodecs ole32)
  target_compile_definitions(fabgl_asset_pipeline PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
  target_sources(fabgl_asset_pipeline PRIVATE src/image_loader_stub.cpp)
endif()

target_include_directories(fabgl_asset_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fabgl_asset_pipeline PUBLIC fabgl_engine)
target_compile_features(fabgl_asset_pipeline PUBLIC cxx_std_20)
fgl_enable_warnings(fabgl_asset_pipeline)
fgl_enable_sanitizers(fabgl_asset_pipeline)

add_executable(fabgl_asset_compiler src/main.cpp)
target_include_directories(fabgl_asset_compiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(fabgl_asset_compiler PRIVATE fabgl_asset_pipeline)
if(WIN32)
  target_link_libraries(fabgl_asset_compiler PRIVATE shell32)
endif()
target_compile_features(fabgl_asset_compiler PRIVATE cxx_std_20)
fgl_enable_warnings(fabgl_asset_compiler)
fgl_enable_sanitizers(fabgl_asset_compiler)

install(TARGETS fabgl_asset_compiler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
