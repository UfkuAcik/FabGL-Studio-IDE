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
  docs/LICENSING.md
  docs/RISKS.md
  docs/FINAL_REPORT.md
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

file(GLOB decision_records RELATIVE "${repository_root}"
  "${repository_root}/docs/decisions/[0-9][0-9][0-9][0-9]-*.md")
if(NOT decision_records)
  list(APPEND missing "docs/decisions/NNNN-*.md")
endif()

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
