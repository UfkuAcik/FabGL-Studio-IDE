add_library(fabgl_toolchain_core STATIC
  src/toolchain_manager.cpp)

target_include_directories(fabgl_toolchain_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fabgl_toolchain_core PUBLIC cxx_std_17)
set_target_properties(fabgl_toolchain_core PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO)
if(COMMAND fgl_enable_warnings)
  fgl_enable_warnings(fabgl_toolchain_core)
endif()
if(COMMAND fgl_enable_sanitizers)
  fgl_enable_sanitizers(fabgl_toolchain_core)
endif()

add_library(FabGLStudio::Toolchain ALIAS fabgl_toolchain_core)

add_executable(fabgl_toolchain_manager
  src/main.cpp)
target_link_libraries(fabgl_toolchain_manager PRIVATE fabgl_toolchain_core)
target_compile_features(fabgl_toolchain_manager PRIVATE cxx_std_17)
set_target_properties(fabgl_toolchain_manager PROPERTIES CXX_STANDARD 17)
if(COMMAND fgl_enable_warnings)
  fgl_enable_warnings(fabgl_toolchain_manager)
endif()

install(TARGETS fabgl_toolchain_manager
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING AND (NOT DEFINED FGL_BUILD_TESTS OR FGL_BUILD_TESTS))
  add_executable(fabgl_toolchain_manager_tests
    tests/toolchain_manager_tests.cpp)
  target_link_libraries(fabgl_toolchain_manager_tests PRIVATE fabgl_toolchain_core)
  target_compile_features(fabgl_toolchain_manager_tests PRIVATE cxx_std_17)
  set_target_properties(fabgl_toolchain_manager_tests PROPERTIES CXX_STANDARD 17)
  target_compile_definitions(fabgl_toolchain_manager_tests PRIVATE
    FGL_TOOLCHAIN_MANIFEST_PATH="${PROJECT_SOURCE_DIR}/toolchains/manifest.json")
  if(COMMAND fgl_enable_warnings)
    fgl_enable_warnings(fabgl_toolchain_manager_tests)
  endif()
  add_test(NAME toolchain_manager COMMAND fabgl_toolchain_manager_tests)

  if(WIN32)
    find_program(FGL_WINDOWS_POWERSHELL_EXECUTABLE
      NAMES powershell.exe powershell)
    if(FGL_WINDOWS_POWERSHELL_EXECUTABLE)
      set(FGL_HARDWARE_LOG_PARSER
        "${PROJECT_SOURCE_DIR}/tests/hardware/parse_fabgl_log.ps1")
      add_test(NAME fabgl_hardware_log_pass
        COMMAND "${FGL_WINDOWS_POWERSHELL_EXECUTABLE}"
          -NoProfile -NonInteractive -ExecutionPolicy Bypass
          -File "${FGL_HARDWARE_LOG_PARSER}"
          -LogPath "${PROJECT_SOURCE_DIR}/tests/hardware/fixtures/sample-pass.log"
          -Json)
      add_test(NAME fabgl_hardware_log_fail
        COMMAND "${FGL_WINDOWS_POWERSHELL_EXECUTABLE}"
          -NoProfile -NonInteractive -ExecutionPolicy Bypass
          -File "${FGL_HARDWARE_LOG_PARSER}"
          -LogPath "${PROJECT_SOURCE_DIR}/tests/hardware/fixtures/sample-fail.log"
          -Json)
      set_tests_properties(fabgl_hardware_log_fail PROPERTIES WILL_FAIL TRUE)
      set_tests_properties(fabgl_hardware_log_pass fabgl_hardware_log_fail
        PROPERTIES LABELS "hardware-log;offline")
    endif()
  endif()
endif()
