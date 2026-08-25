cmake_minimum_required(VERSION 3.20)

if(NOT PROJECT_CLI OR NOT EXISTS "${PROJECT_CLI}")
  message(FATAL_ERROR "PROJECT_CLI must name the built project CLI")
endif()
if(NOT TEST_ROOT)
  message(FATAL_ERROR "TEST_ROOT must name an owned test output directory")
endif()

get_filename_component(TEST_ROOT "${TEST_ROOT}" ABSOLUTE)
get_filename_component(test_root_name "${TEST_ROOT}" NAME)
if(NOT test_root_name STREQUAL "guid-process-test")
  message(FATAL_ERROR "TEST_ROOT must end in the owned directory name guid-process-test")
endif()
set(ownership_marker "${TEST_ROOT}/.fabgl-guid-process-test")
if(EXISTS "${TEST_ROOT}")
  if(NOT EXISTS "${ownership_marker}")
    message(FATAL_ERROR "Refusing to replace an unowned GUID test directory: ${TEST_ROOT}")
  endif()
  file(REMOVE_RECURSE "${TEST_ROOT}")
endif()
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(WRITE "${ownership_marker}" "owned by fabgl_cross_process_guid_tests\n")

function(create_test_project directory name)
  execute_process(
    COMMAND "${PROJECT_CLI}" new "${directory}" "${name}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "Creating ${name} failed with ${result}:\nstdout: ${output}\nstderr: ${error}")
  endif()
endfunction()

set(first_root "${TEST_ROOT}/first")
set(second_root "${TEST_ROOT}/second")
create_test_project("${first_root}" "First")
create_test_project("${second_root}" "Second")

file(READ "${first_root}/First.fglproject" first_project)
file(READ "${second_root}/Second.fglproject" second_project)
file(READ "${first_root}/Scenes/Main.fglscene" first_scene)
file(READ "${second_root}/Scenes/Main.fglscene" second_scene)

function(extract_guid source_name result_name pattern)
  string(REGEX MATCH "${pattern}" matched "${${source_name}}")
  if(NOT matched)
    message(FATAL_ERROR "Could not extract ${result_name} from ${source_name}")
  endif()
  set("${result_name}" "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

extract_guid(first_project first_project_guid
  "\"projectGuid\"[ \t]*:[ \t]*\"([0-9a-f-]+)\"")
extract_guid(second_project second_project_guid
  "\"projectGuid\"[ \t]*:[ \t]*\"([0-9a-f-]+)\"")
extract_guid(first_scene first_scene_guid "scene_guid[ \t]+([0-9a-f-]+)")
extract_guid(second_scene second_scene_guid "scene_guid[ \t]+([0-9a-f-]+)")
extract_guid(first_scene first_entity_guid "entity_begin[\r\n]+guid[ \t]+([0-9a-f-]+)")
extract_guid(second_scene second_entity_guid "entity_begin[\r\n]+guid[ \t]+([0-9a-f-]+)")

if(first_project_guid STREQUAL second_project_guid)
  message(FATAL_ERROR
    "Separate project CLI processes generated the same project GUID: ${first_project_guid}")
endif()
if(first_scene_guid STREQUAL second_scene_guid)
  message(FATAL_ERROR
    "Separate project CLI processes generated the same scene GUID: ${first_scene_guid}")
endif()
if(first_entity_guid STREQUAL second_entity_guid)
  message(FATAL_ERROR
    "Separate project CLI processes generated the same entity GUID: ${first_entity_guid}")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
message(STATUS "Separate project CLI processes generated distinct project, scene and entity GUIDs")
