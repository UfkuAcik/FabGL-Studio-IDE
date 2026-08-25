cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS PROJECT_CLI PLAYER EXTENSION_MODULE REPOSITORY_ROOT TEST_ROOT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

function(require_success label result output error)
  if(NOT ${result} EQUAL 0)
    message(FATAL_ERROR "${label} failed (${${result}})\nstdout:\n${${output}}\nstderr:\n${${error}}")
  endif()
endfunction()

function(make_project name package_kind package_id out_project out_trace)
  set(project_root "${TEST_ROOT}/${name}")
  set(package_root "${TEST_ROOT}/${name}-package")
  file(MAKE_DIRECTORY "${project_root}/Scenes" "${package_root}/bin")
  file(COPY_FILE "${REPOSITORY_ROOT}/examples/empty/Empty.fglproject"
                 "${project_root}/ExtensionTest.fglproject")
  file(COPY_FILE "${REPOSITORY_ROOT}/examples/empty/Scenes/Main.fglscene"
                 "${project_root}/Scenes/Main.fglscene")
  get_filename_component(module_name "${EXTENSION_MODULE}" NAME)
  file(COPY_FILE "${EXTENSION_MODULE}" "${package_root}/bin/${module_name}")
  file(WRITE "${package_root}/fabgl.package"
    "schema=2\n"
    "id=${package_id}\n"
    "displayName=Player Extension Fixture\n"
    "version=1.0.0\n"
    "engine=*\n"
    "author=FabGL Tests\n"
    "license=MIT\n"
    "executable=true\n"
    "entry=${package_kind}:bin/${module_name}\n")
  set(project "${project_root}/ExtensionTest.fglproject")
  execute_process(
    COMMAND "${PROJECT_CLI}" package install "${project}" "${package_root}" --allow-executable
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
  require_success("package install" install_result install_output install_error)
  set(${out_project} "${project}" PARENT_SCOPE)
  set(${out_trace} "${TEST_ROOT}/${name}-trace.txt" PARENT_SCOPE)
endfunction()

function(run_player label project trace expected_modules)
  file(REMOVE "${trace}")
  set(service_trace "${trace}.services")
  file(REMOVE "${service_trace}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "FGL_EXTENSION_FIXTURE_TRACE=${trace}"
            "FGL_EXTENSION_FIXTURE_SERVICE_TRACE=${service_trace}"
            "${PLAYER}" --project "${project}" --headless --frames 1 ${ARGN}
    RESULT_VARIABLE player_result
    OUTPUT_VARIABLE player_output
    ERROR_VARIABLE player_error)
  require_success("${label}" player_result player_output player_error)
  if(NOT player_output MATCHES "extension_modules=${expected_modules} extensions=${expected_modules}")
    message(FATAL_ERROR "${label} reported unexpected extension counts\n${player_output}")
  endif()
  if(NOT player_output MATCHES "extension_services=${expected_modules} extension_service_failures=0")
    message(FATAL_ERROR "${label} reported unexpected service counts\n${player_output}")
  endif()
endfunction()

make_project("runtime" "runtime-module" "test.player-runtime" runtime_project runtime_trace)
run_player("normal runtime extension player" "${runtime_project}" "${runtime_trace}" 1)
if(NOT EXISTS "${runtime_trace}")
  message(FATAL_ERROR "normal player did not dispatch extension lifecycle hooks")
endif()
file(READ "${runtime_trace}" runtime_events)
string(CONCAT expected_events
  "fabgl.project.open|open|player\n"
  "fabgl.runtime.start|start|player\n"
  "fabgl.runtime.stop|stop|player\n"
  "fabgl.project.close|close|player\n")
if(NOT runtime_events STREQUAL expected_events)
  message(FATAL_ERROR "player extension lifecycle order is wrong\n${runtime_events}")
endif()
set(runtime_service_trace "${runtime_trace}.services")
if(NOT EXISTS "${runtime_service_trace}")
  message(FATAL_ERROR "normal player did not dispatch registered runtime services")
endif()
file(READ "${runtime_service_trace}" runtime_service_events)
string(REGEX MATCHALL "startup\\|[^\n]*|update\\|[^\n]*|shutdown\\|[^\n]*"
       runtime_service_records "${runtime_service_events}")
list(LENGTH runtime_service_records runtime_service_count)
if(NOT runtime_service_count EQUAL 3 OR
   NOT runtime_service_events MATCHES "startup\\|\\{\"schema\":1,\"host\":\"player\"\\}" OR
   NOT runtime_service_events MATCHES "update\\|\\{\"schema\":1,\"deltaSeconds\":" OR
   NOT runtime_service_events MATCHES "shutdown\\|\\{\"schema\":1,\"reason\":\"player\"\\}")
  message(FATAL_ERROR "player runtime service startup/update/shutdown contract is wrong\n${runtime_service_events}")
endif()

run_player("disabled extension player" "${runtime_project}" "${runtime_trace}" 0
           --disable-plugins)
if(EXISTS "${runtime_trace}")
  message(FATAL_ERROR "--disable-plugins loaded or invoked native extension code")
endif()
if(EXISTS "${runtime_trace}.services")
  message(FATAL_ERROR "--disable-plugins dispatched a native extension service")
endif()

run_player("safe-mode extension player" "${runtime_project}" "${runtime_trace}" 0 --safe-mode)
if(EXISTS "${runtime_trace}")
  message(FATAL_ERROR "--safe-mode loaded or invoked native extension code")
endif()
if(EXISTS "${runtime_trace}.services")
  message(FATAL_ERROR "--safe-mode dispatched a native extension service")
endif()

make_project("editor" "editor-plugin" "test.player-editor" editor_project editor_trace)
run_player("editor-kind filter player" "${editor_project}" "${editor_trace}" 0)
if(EXISTS "${editor_trace}")
  message(FATAL_ERROR "player loaded an editor-only extension")
endif()

message(STATUS "PC player extension loading, filtering, lifecycle, and disabled modes passed")
