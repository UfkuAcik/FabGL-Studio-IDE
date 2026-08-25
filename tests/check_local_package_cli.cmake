if(NOT DEFINED PROJECT_CLI OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "PROJECT_CLI and TEST_ROOT are required")
endif()

function(run_cli description)
    execute_process(
        COMMAND "${PROJECT_CLI}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output
        ENCODING UTF-8)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed (${result})\nstdout:\n${output}\nstderr:\n${error_output}")
    endif()
    set(CLI_OUTPUT "${output}" PARENT_SCOPE)
    set(CLI_ERROR "${error_output}" PARENT_SCOPE)
endfunction()

function(run_cli_expect_failure description expected_text)
    execute_process(
        COMMAND "${PROJECT_CLI}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output
        ENCODING UTF-8)
    if(result EQUAL 0)
        message(FATAL_ERROR "${description} unexpectedly succeeded\nstdout:\n${output}")
    endif()
    string(FIND "${output}\n${error_output}" "${expected_text}" match)
    if(match EQUAL -1)
        message(FATAL_ERROR
            "${description} did not report '${expected_text}'\nstdout:\n${output}\nstderr:\n${error_output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/BasePackage/Assets")
file(MAKE_DIRECTORY "${TEST_ROOT}/ExtensionPackage/src")
file(MAKE_DIRECTORY "${TEST_ROOT}/TraversalPackage")

run_cli("project creation" new "${TEST_ROOT}/Project" "Package CLI Smoke")
set(project "${TEST_ROOT}/Project/Package CLI Smoke.fglproject")
run_cli("empty package listing" package list "${project}")
if(NOT CLI_OUTPUT MATCHES "packages=0")
    message(FATAL_ERROR "empty package list output is unexpected: ${CLI_OUTPUT}")
endif()

file(WRITE "${TEST_ROOT}/BasePackage/fabgl.package"
    "schema=2\nid=cli.base\ndisplayName=CLI Base\nversion=1.1.0\nengine=*\n"
    "author=FabGL Tests\nlicense=MIT\nexecutable=false\n")
file(WRITE "${TEST_ROOT}/BasePackage/Assets/data.txt" "base package\n")
run_cli("data package install" package install "${project}" "${TEST_ROOT}/BasePackage")
if(NOT CLI_OUTPUT MATCHES "installed cli.base@1.1.0")
    message(FATAL_ERROR "install output is unexpected: ${CLI_OUTPUT}")
endif()
file(READ "${TEST_ROOT}/Project/Packages/fabgl-packages.lock" lock_before)
file(READ "${TEST_ROOT}/Project/Packages/.fabgl-package-trust" trust_before)

file(WRITE "${TEST_ROOT}/ExtensionPackage/fabgl.package"
    "schema=2\nid=cli.extension\ndisplayName=CLI Extension\nversion=2.0.0\nengine=*\n"
    "author=FabGL Tests\nlicense=Apache-2.0\ntrust=trusted\nexecutable=false\n"
    "dependency=cli.base@^1.0.0\nentry=runtime-module:src/plugin.cpp\n")
file(WRITE "${TEST_ROOT}/ExtensionPackage/src/plugin.cpp" "void cli_package_entry() {}\n")
run_cli_expect_failure("unapproved executable install" "--allow-executable"
    package install "${project}" "${TEST_ROOT}/ExtensionPackage")
if(EXISTS "${TEST_ROOT}/Project/Packages/cli.extension")
    message(FATAL_ERROR "failed executable install left a package directory")
endif()
file(READ "${TEST_ROOT}/Project/Packages/fabgl-packages.lock" lock_after_rejection)
file(READ "${TEST_ROOT}/Project/Packages/.fabgl-package-trust" trust_after_rejection)
if(NOT "${lock_before}" STREQUAL "${lock_after_rejection}" OR
   NOT "${trust_before}" STREQUAL "${trust_after_rejection}")
    message(FATAL_ERROR "failed executable install changed package metadata")
endif()

run_cli("approved executable install" package install "${project}"
    "${TEST_ROOT}/ExtensionPackage" --allow-executable)
file(READ "${TEST_ROOT}/Project/Packages/.fabgl-package-trust" trust_after_approval)
if(NOT trust_after_approval MATCHES "allow=cli.extension@2.0.0#[0-9a-f]+")
    message(FATAL_ERROR "approved executable install has no content-bound trust record")
endif()
run_cli("package validation" package validate "${project}")
if(NOT CLI_OUTPUT MATCHES "load-order=cli.base,cli.extension")
    message(FATAL_ERROR "dependency load order is unexpected: ${CLI_OUTPUT}")
endif()
run_cli_expect_failure("dependency-protected removal" "still depends"
    package remove "${project}" cli.base)
run_cli("extension removal" package remove "${project}" cli.extension)
run_cli("base removal" package remove "${project}" cli.base)
run_cli("empty package validation" package validate "${project}")

file(WRITE "${TEST_ROOT}/TraversalPackage/fabgl.package"
    "schema=2\nid=cli.traversal\ndisplayName=CLI Traversal\nversion=1.0.0\nengine=*\n"
    "author=FabGL Tests\nlicense=MIT\nexecutable=true\n"
    "entry=runtime-module:../escape.cpp\n")
run_cli_expect_failure("traversal manifest rejection" "entry point is invalid"
    package install "${project}" "${TEST_ROOT}/TraversalPackage" --allow-executable)

# Directory links are rejected where the host permits creating this fixture. Windows systems
# without Developer Mode commonly refuse the fixture itself, so that case remains covered by the
# manager's native reparse-point checks and the unit-level malicious manifest fixtures.
file(MAKE_DIRECTORY "${TEST_ROOT}/LinkedPackage")
file(MAKE_DIRECTORY "${TEST_ROOT}/ExternalPayload")
file(WRITE "${TEST_ROOT}/LinkedPackage/fabgl.package"
    "schema=2\nid=cli.linked\ndisplayName=CLI Linked\nversion=1.0.0\nengine=*\n"
    "author=FabGL Tests\nlicense=MIT\nexecutable=false\n")
file(WRITE "${TEST_ROOT}/ExternalPayload/data.txt" "outside\n")
file(CREATE_LINK "${TEST_ROOT}/ExternalPayload" "${TEST_ROOT}/LinkedPackage/linked"
    SYMBOLIC RESULT link_result)
if(link_result STREQUAL "0")
    run_cli_expect_failure("directory-link rejection" "symbolic link"
        package install "${project}" "${TEST_ROOT}/LinkedPackage")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
