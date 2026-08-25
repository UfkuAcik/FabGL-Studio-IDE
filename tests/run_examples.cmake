cmake_minimum_required(VERSION 3.20)

foreach(required PROJECT_CLI PLAYER REPOSITORY_ROOT TEST_OUTPUT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} was not provided")
  endif()
endforeach()

set(example_cases
  # manifest|framebuffer-checksum|entities|visual-assets|mode|minimum-draws|required-component|runtime-contract
  "empty/Empty.fglproject|5444902038255606565|0|0|2d|0|EMPTY|NONE"
  "platformer/Platformer.fglproject|8539681250806078981|8|1|2d|3|CharacterBody2D|PLATFORMER"
  "top_down/TopDown.fglproject|15569116889284863909|5|1|2d|2|CharacterBody2D|TOPDOWN"
  "raycast_fps/RaycastFPS.fglproject|17420299176440120981|6|1|raycast|1|RaycastMap|FPS"
  "pseudo3d_racer/Racer.fglproject|3106091432737188430|3|2|racer|1|VehicleController|RACER"
  "tps_technology/TPS.fglproject|11399129820573504273|5|1|lowpoly|5|ThirdPersonController|TPS"
  "ui_showcase/UIShowcase.fglproject|9705650724915765417|3|0|2d|8|UITransform|UI"
  "audio_showcase/AudioShowcase.fglproject|5444902038255606565|3|1|2d|0|AudioSource|AUDIO"
  "animation_showcase/AnimationShowcase.fglproject|17517198995855967059|2|3|2d|1|Animator|ANIMATION"
  "asset_streaming/AssetStreaming.fglproject|5866886360010054917|2|4|2d|201|TilemapRenderer|NONE")

foreach(example_case IN LISTS example_cases)
  string(REPLACE "|" ";" fields "${example_case}")
  list(GET fields 0 relative_manifest)
  list(GET fields 1 checksum)
  list(GET fields 2 entity_count)
  list(GET fields 3 visual_asset_count)
  list(GET fields 4 presentation_mode)
  list(GET fields 5 minimum_draws)
  list(GET fields 6 required_component)
  list(GET fields 7 runtime_contract)
  set(manifest "${REPOSITORY_ROOT}/examples/${relative_manifest}")
  get_filename_component(example_directory "${manifest}" DIRECTORY)
  set(scene "${example_directory}/Scenes/Main.fglscene")
  if(NOT EXISTS "${scene}")
    message(FATAL_ERROR "Example scene is missing for ${relative_manifest}: ${scene}")
  endif()
  file(STRINGS "${scene}" scene_header LIMIT_COUNT 1)
  if(NOT scene_header STREQUAL "fglscene 2")
    message(FATAL_ERROR
      "Example scene is not in the canonical v2 format: ${relative_manifest}")
  endif()
  file(READ "${scene}" scene_text)
  if(required_component STREQUAL "EMPTY")
    if(scene_text MATCHES "entity_begin")
      message(FATAL_ERROR "Empty example contains an entity: ${relative_manifest}")
    endif()
  elseif(NOT scene_text MATCHES "type_name \"fabgl\\.${required_component}\"")
    message(FATAL_ERROR
      "Example does not serialize its required component ${required_component}: ${relative_manifest}")
  endif()

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
    COMMAND "${PLAYER}" --project "${manifest}" --headless --frames 180
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
  if(NOT player_output MATCHES
      "project=\"[^\"]+\" entities=${entity_count} visual_assets=${visual_asset_count} mode=${presentation_mode} draws=[0-9]+ missing_assets=0 native_scripts=0")
    message(FATAL_ERROR
      "Player did not report the expected project-driven scene/assets/runtime for ${relative_manifest}:\n${player_output}")
  endif()
  string(REGEX MATCH "draws=([0-9]+)" draw_match "${player_output}")
  set(draw_count "${CMAKE_MATCH_1}")
  if(draw_count STREQUAL "" OR draw_count LESS minimum_draws)
    message(FATAL_ERROR
      "Player draw evidence is below the example contract for ${relative_manifest}:\n${player_output}")
  endif()

  if(runtime_contract STREQUAL "PLATFORMER")
    if(NOT player_output MATCHES
        "gameplay_updates=[1-9][0-9]* platformer_collectibles=[1-9][0-9]* platformer_damage=[1-9][0-9]* platformer_checkpoints=[1-9][0-9]* platformer_transitions=[1-9][0-9]* platformer_health=[1-9][0-9]*")
      message(FATAL_ERROR
        "Platformer example did not advance world interactions/HUD state:\n${player_output}")
    endif()
  elseif(runtime_contract STREQUAL "TOPDOWN")
    if(NOT player_output MATCHES
        "topdown_shots=[1-9][0-9]* topdown_hits=[1-9][0-9]* topdown_pickups=[1-9][0-9]* topdown_enemies=[0-9]+ topdown_rooms=[1-9][0-9]*")
      message(FATAL_ERROR
        "Top-down example did not advance weapons/enemies/pickups/room state:\n${player_output}")
    endif()
  elseif(runtime_contract STREQUAL "FPS")
    if(NOT player_output MATCHES
        "fps_shots=[1-9][0-9]* fps_hits=[1-9][0-9]* fps_doors=[1-9][0-9]* fps_pickups=[1-9][0-9]* fps_keys=[0-9]+ fps_secrets=[1-9][0-9]*")
      message(FATAL_ERROR
        "FPS example did not advance weapon/door/key/pickup/secret state:\n${player_output}")
    endif()
  elseif(runtime_contract STREQUAL "TPS")
    if(NOT player_output MATCHES
        "tps_shots=[1-9][0-9]* tps_hits=[1-9][0-9]* tps_pickups=[1-9][0-9]* tps_targets=[1-9][0-9]*")
      message(FATAL_ERROR
        "TPS example did not advance target/weapon/pickup state:\n${player_output}")
    endif()
  elseif(runtime_contract STREQUAL "RACER")
    if(NOT player_output MATCHES
        "racer_opponents=[1-9][0-9]* racer_checkpoints=[1-9][0-9]* racer_position=[1-9][0-9]* racer_lap=[1-9][0-9]* racer_gear=[1-9][0-9]* racer_speed_kph=[1-9][0-9]* racer_countdown=0 racer_finished=[01]")
      message(FATAL_ERROR
        "Racer example did not advance countdown/gears/AI/rank/lap/HUD state:\n${player_output}")
    endif()
  elseif(runtime_contract STREQUAL "ANIMATION")
    if(NOT player_output MATCHES
        "animation_clips=1 animator_controllers=1 animators=1 animation_samples=180 animation_changed_samples=[1-9][0-9]*")
      message(FATAL_ERROR
        "Animation example did not resolve and update its serialized Animator:\n${player_output}")
    endif()
  elseif(runtime_contract STREQUAL "AUDIO")
    if(NOT player_output MATCHES "audio_clips=1"
       OR NOT player_output MATCHES
          "asset_resident_bytes=[1-9][0-9]* asset_stream_loads=1 asset_stream_evictions=0 asset_stream_transitions=1"
       OR NOT player_output MATCHES
          "audio_listeners=1 audio_sources=2 audio_voices_started=2 audio_mixed_frames=144000 audio_nonzero_samples=[1-9][0-9]* audio_checksum=11862043089326115411")
      message(FATAL_ERROR
        "Audio example did not bind and deterministically mix its serialized sources:\n${player_output}")
    endif()
  elseif(runtime_contract STREQUAL "UI")
    if(NOT player_output MATCHES "ui_widgets=3 ui_glyphs=15")
      message(FATAL_ERROR
        "UI example did not submit its runtime widgets and text glyphs:\n${player_output}")
    endif()
  endif()
endforeach()

set(replay_project "${REPOSITORY_ROOT}/examples/platformer/Platformer.fglproject")
execute_process(
  COMMAND "${PLAYER}" --project "${replay_project}" --headless --frames 180
          --record-input "${TEST_OUTPUT}"
  RESULT_VARIABLE record_result
  OUTPUT_VARIABLE record_output
  ERROR_VARIABLE record_error)
if(NOT record_result EQUAL 0 OR NOT EXISTS "${TEST_OUTPUT}")
  message(FATAL_ERROR "Input recording failed:\n${record_output}${record_error}")
endif()
execute_process(
  COMMAND "${PLAYER}" --project "${replay_project}" --headless --frames 180
          --replay-input "${TEST_OUTPUT}"
  RESULT_VARIABLE replay_result
  OUTPUT_VARIABLE replay_output
  ERROR_VARIABLE replay_error)
if(NOT replay_result EQUAL 0)
  message(FATAL_ERROR "Recorded input replay failed:\n${replay_output}${replay_error}")
endif()
string(REGEX MATCH "checksum=([0-9]+)" record_checksum "${record_output}")
set(record_checksum_value "${CMAKE_MATCH_1}")
string(REGEX MATCH "checksum=([0-9]+)" replay_checksum "${replay_output}")
set(replay_checksum_value "${CMAKE_MATCH_1}")
if(record_checksum_value STREQUAL "" OR
   NOT record_checksum_value STREQUAL replay_checksum_value)
  message(FATAL_ERROR
    "Recorded replay was not deterministic:\nrecord=${record_output}\nreplay=${replay_output}")
endif()

message(STATUS "Validated and replayed project-backed examples: ${example_cases}")
