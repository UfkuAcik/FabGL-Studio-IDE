if(NOT DEFINED PROJECT_CLI OR NOT EXISTS "${PROJECT_CLI}")
    message(FATAL_ERROR "PROJECT_CLI must identify fabgl_project_cli")
endif()
if(NOT DEFINED PLAYER OR NOT EXISTS "${PLAYER}")
    message(FATAL_ERROR "PLAYER must identify fabgl_player_pc")
endif()
if(NOT DEFINED REPOSITORY_ROOT OR NOT IS_DIRECTORY "${REPOSITORY_ROOT}")
    message(FATAL_ERROR "REPOSITORY_ROOT must identify the source tree")
endif()
if(NOT DEFINED TEST_ROOT OR TEST_ROOT STREQUAL "" OR TEST_ROOT STREQUAL "/")
    message(FATAL_ERROR "TEST_ROOT must be an explicit bounded test directory")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

set(_project "${REPOSITORY_ROOT}/examples/animation_showcase/AnimationShowcase.fglproject")
set(_first "${TEST_ROOT}/first")
set(_second "${TEST_ROOT}/second")
foreach(_output IN ITEMS "${_first}" "${_second}")
    execute_process(
        COMMAND "${PROJECT_CLI}" prepare "${_project}" "${_output}" pc
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "project prepare failed (${_result}):\n${_stdout}\n${_stderr}")
    endif()
    if(NOT _stdout MATCHES "assets=4" OR NOT _stdout MATCHES "validated=4")
        message(FATAL_ERROR "project prepare did not report the expected bounded assets:\n${_stdout}")
    endif()
    if(NOT EXISTS "${_output}/project/Prepared.fglproject" OR
       NOT EXISTS "${_output}/project-assets.fglpack" OR
       NOT EXISTS "${_output}/project/Assets/AnimatedCharacter.fglprefab")
        message(FATAL_ERROR "project prepare omitted a required artifact")
    endif()
    file(READ "${_output}/project/Prepared.fglproject" _prepared_manifest)
    if(NOT _prepared_manifest MATCHES
       "54000000-0000-4000-8000-000000000009.*AnimatedCharacter.fglprefab.*prefab")
        message(FATAL_ERROR "prepared project omitted the linked prefab GUID/type mapping")
    endif()
endforeach()

file(SHA256 "${_first}/project-assets.fglpack" _first_hash)
file(SHA256 "${_second}/project-assets.fglpack" _second_hash)
if(NOT _first_hash STREQUAL _second_hash)
    message(FATAL_ERROR "project prepare output is not deterministic")
endif()

execute_process(
    COMMAND "${PROJECT_CLI}" validate "${_first}/project/Prepared.fglproject"
    RESULT_VARIABLE _validate_result
    OUTPUT_VARIABLE _validate_stdout
    ERROR_VARIABLE _validate_stderr)
if(NOT _validate_result EQUAL 0)
    message(FATAL_ERROR
        "prepared project validation failed:\n${_validate_stdout}\n${_validate_stderr}")
endif()

execute_process(
    COMMAND "${PLAYER}" --project "${_first}/project/Prepared.fglproject"
            --headless --frames 12
    RESULT_VARIABLE _player_result
    OUTPUT_VARIABLE _player_stdout
    ERROR_VARIABLE _player_stderr)
if(NOT _player_result EQUAL 0)
    message(FATAL_ERROR "prepared player failed:\n${_player_stdout}\n${_player_stderr}")
endif()
if(NOT _player_stdout MATCHES "animators=1" OR
   NOT _player_stdout MATCHES "animation_changed_samples=12" OR
   NOT _player_stdout MATCHES "missing_assets=0")
    message(FATAL_ERROR "prepared player did not consume the real animation assets:\n${_player_stdout}")
endif()

message(STATUS "Project prepare CLI, determinism, validation, and player consumption passed")
