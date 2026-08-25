cmake_minimum_required(VERSION 3.24)

get_filename_component(repository_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(required_docs
  README.md
  ROADMAP.md
  ARCHITECTURE.md
  BUILDING.md
  USER_GUIDE.md
  EDITOR_GUIDE.md
  SCRIPTING_API.md
  ENGINE_API.md
  FILE_FORMATS.md
  ASSET_PIPELINE.md
  RENDERERS.md
  PERFORMANCE_BUDGETS.md
  HARDWARE_TESTING.md
  TOOLCHAIN.md
  PLUGIN_DEVELOPMENT.md
  PACKAGE_FORMAT.md
  CONTRIBUTING.md
  CODE_OF_CONDUCT.md
  SECURITY.md
  CHANGELOG.md
  THIRD_PARTY_LICENSES.md
  LICENSE
  NOTICE
  docs/ASSUMPTIONS.md
  docs/HANDOFF.md
  docs/COMPLETION_AUDIT.md
  docs/LICENSING.md
  docs/RISKS.md
  docs/FINAL_REPORT.md
  docs/images/studio-overview.png
  docs/progress/README.md
  docs/progress/M00-discovery.md
  docs/progress/M01-editor-shell.md
  docs/progress/M02-engine-core.md
  docs/progress/M03-pc-player-2d.md
  docs/progress/M04-editor-engine.md
  docs/progress/M05-assets.md
  docs/progress/M06-fabgl-toolchain.md
  docs/progress/M07-cpp-scripting.md
  docs/progress/M08-prefab-animation.md
  docs/progress/M09-visual-scripting.md
  docs/progress/M10-raycast-fps.md
  docs/progress/M11-racer.md
  docs/progress/M12-lowpoly-tps.md
  docs/progress/M13-advanced-tools.md
  docs/progress/M14-productization.md
  docs/progress/M15-quality.md)

set(missing "")
set(empty "")
foreach(relative_path IN LISTS required_docs)
  set(absolute_path "${repository_root}/${relative_path}")
  if(NOT EXISTS "${absolute_path}")
    list(APPEND missing "${relative_path}")
  elseif(IS_DIRECTORY "${absolute_path}")
    list(APPEND empty "${relative_path} (is a directory)")
  else()
    file(SIZE "${absolute_path}" document_size)
    if(document_size EQUAL 0)
      list(APPEND empty "${relative_path}")
    endif()
  endif()
endforeach()

set(studio_screenshot "${repository_root}/docs/images/studio-overview.png")
if(EXISTS "${studio_screenshot}")
  file(READ "${studio_screenshot}" studio_screenshot_signature LIMIT 8 HEX)
  string(TOLOWER "${studio_screenshot_signature}" studio_screenshot_signature)
  if(NOT studio_screenshot_signature STREQUAL "89504e470d0a1a0a")
    list(APPEND empty "docs/images/studio-overview.png (not a PNG)")
  endif()
endif()

file(GLOB decision_records RELATIVE "${repository_root}"
  "${repository_root}/docs/decisions/[0-9][0-9][0-9][0-9]-*.md")
if(NOT decision_records)
  list(APPEND missing "docs/decisions/NNNN-*.md")
endif()

set(required_decision_records
  docs/decisions/0001-portable-core-and-qt-editor.md
  docs/decisions/0002-editor-friendly-ecs.md
  docs/decisions/0003-versioned-source-and-binary-pack.md
  docs/decisions/0004-toolchain-and-fabgl.md
  docs/decisions/0005-gpl-project-license.md
  docs/decisions/0009-source-extension-host-before-stable-binary-abi.md
  docs/decisions/0010-software-presentation-backends-before-sdl.md
  docs/decisions/0011-reflected-properties-with-explicit-codecs.md
  docs/decisions/0012-stable-asset-guid-references.md
  docs/decisions/0013-versioned-native-script-module-boundary.md
  docs/decisions/0014-validated-visual-graph-bytecode.md
  docs/decisions/0015-modular-renderers-over-a-bounded-framebuffer.md
  docs/decisions/0016-shared-scene-runtime-with-platform-adapters.md
  docs/decisions/0017-fixed-capacity-target-runtime-memory.md
  docs/decisions/0018-separate-bounded-esp32-gameplay-companion.md)
list(APPEND required_decision_records
  docs/decisions/0019-separate-transactional-esp32-save-codec.md)
foreach(relative_path IN LISTS required_decision_records)
  if(NOT EXISTS "${repository_root}/${relative_path}")
    list(APPEND missing "${relative_path}")
  endif()
endforeach()

set(required_adr_sections
  "# ADR"
  "Status"
  "## Context"
  "Options considered"
  "## Decision"
  "## Rationale"
  "Positive"
  "Negative"
  "Reconsider")
foreach(relative_path IN LISTS decision_records)
  file(READ "${repository_root}/${relative_path}" adr_text)
  foreach(required_section IN LISTS required_adr_sections)
    string(FIND "${adr_text}" "${required_section}" section_position)
    if(section_position EQUAL -1)
      list(APPEND empty "${relative_path} (missing ADR section: ${required_section})")
    endif()
  endforeach()
endforeach()

if(missing)
  list(JOIN missing ", " missing_text)
  message(FATAL_ERROR "Required documentation is missing: ${missing_text}")
endif()
if(empty)
  list(JOIN empty ", " empty_text)
  message(FATAL_ERROR "Required documentation is empty or invalid: ${empty_text}")
endif()

list(LENGTH required_docs required_doc_count)
list(LENGTH decision_records decision_count)
message(STATUS
  "Documentation contract satisfied: ${required_doc_count} required files and "
  "${decision_count} architecture decision record(s)")
