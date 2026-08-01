cmake_minimum_required(VERSION 3.20)

foreach(required PROJECT_CLI PLAYER REPOSITORY_ROOT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} was not provided")
  endif()
endforeach()

set(example_cases
  "empty/Empty.fglproject|empty|3684892159837637615"
  "platformer/Platformer.fglproject|platformer|4674214439384696037"
  "top_down/TopDown.fglproject|topdown|6529423119449967289"
  "raycast_fps/RaycastFPS.fglproject|raycast|14763779848664866702"
  "pseudo3d_racer/Racer.fglproject|racer|6713019085718644632"
  "tps_technology/TPS.fglproject|lowpoly|13254507645075047209"
  "ui_showcase/UIShowcase.fglproject|ui|6744144588786156117"
  "audio_showcase/AudioShowcase.fglproject|audio|4061273253253862341"
  "animation_showcase/AnimationShowcase.fglproject|animation|13752941473616129720"
  "asset_streaming/AssetStreaming.fglproject|streaming|15684697036675318470")

foreach(example_case IN LISTS example_cases)
  string(REPLACE "|" ";" fields "${example_case}")
  list(GET fields 0 relative_manifest)
  list(GET fields 1 demo)
  list(GET fields 2 checksum)
  set(manifest "${REPOSITORY_ROOT}/examples/${relative_manifest}")

  execute_process(
    COMMAND "${PROJECT_CLI}" validate "${manifest}"
    RESULT_VARIABLE validate_result
    OUTPUT_VARIABLE validate_output
    ERROR_VARIABLE validate_error)
  if(NOT validate_result EQUAL 0)
    message(FATAL_ERROR
      "Project validation failed for ${relative_manifest}:\n${validate_output}${validate_error}")
  endif()

  execute_process(
    COMMAND "${PLAYER}" --demo "${demo}" --headless --frames 180
    RESULT_VARIABLE player_result
    OUTPUT_VARIABLE player_output
    ERROR_VARIABLE player_error)
  if(NOT player_result EQUAL 0)
    message(FATAL_ERROR
      "Player failed for ${relative_manifest}:\n${player_output}${player_error}")
  endif()
  if(NOT player_output MATCHES "checksum=${checksum}([\r\n]|$)")
    message(FATAL_ERROR
      "Golden output mismatch for ${relative_manifest}:\n${player_output}${player_error}")
  endif()
endforeach()

message(STATUS "Validated and replayed ${example_cases}")
