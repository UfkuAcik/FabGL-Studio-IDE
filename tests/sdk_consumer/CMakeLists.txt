cmake_minimum_required(VERSION 3.24)

project(FabGLStudioSdkConsumer LANGUAGES CXX)

find_package(FabGLStudio CONFIG REQUIRED)

add_executable(fabgl_sdk_consumer main.cpp)
target_compile_features(fabgl_sdk_consumer PRIVATE cxx_std_20)
target_link_libraries(fabgl_sdk_consumer PRIVATE
  FabGLStudio::Engine
  FabGLStudio::Renderers
  FabGLStudio::Frameworks
  FabGLStudio::AssetPipeline)
