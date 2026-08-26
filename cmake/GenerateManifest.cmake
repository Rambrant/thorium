#
# Runs at install time (see framework/runner/CMakeLists.txt's install(CODE ...)/
# install(SCRIPT ...) pair right below its install(TARGETS run_scripts ...))
# -- asks the just-installed run_scripts binary for its own test catalog
# (--list-tests) and writes that out as manifest.json next to it. This is
# what a discovering web server reads to offer "which suite, which tests,
# which criteria variant" without parsing C++ or knowing anything about
# this build beyond where run_scripts landed.
#
# criteriaVariants is a list rather than the single criteriaVariant this used
# to report, and that is the visible half of a real change: every variant is
# now compiled into the binary and chosen per run with --criteria=, so one
# installed run_scripts offers what previously needed one install per variant.
# A server that wants to run the stress tolerances no longer has to find a
# differently-built binary -- it passes a flag to this one. defaultCriteria is
# what it gets if it passes nothing.
#
# THORIUM_RUN_SCRIPTS_EXE / THORIUM_KNOWN_CRITERIA_VARIANTS /
# THORIUM_CRITERIA_VARIANT / THORIUM_MANIFEST_OUTPUT are set by the
# install(CODE ...) calls immediately before this script runs, not passed as -D
# arguments -- install(SCRIPT ...) shares the same variable scope as the
# install(CODE ...) calls around it in cmake_install.cmake, the ordinary way to
# hand values into an install-time script.
#
execute_process(
    COMMAND "${THORIUM_RUN_SCRIPTS_EXE}" --list-tests
    OUTPUT_VARIABLE THORIUM_CATALOG_RAW
    RESULT_VARIABLE THORIUM_CATALOG_RESULT
)

if(NOT THORIUM_CATALOG_RESULT EQUAL 0)
    message(FATAL_ERROR "run_scripts --list-tests failed while generating manifest.json")
endif()

string(REPLACE "\n" ";" THORIUM_CATALOG_LINES "${THORIUM_CATALOG_RAW}")

#
# Turns "group|id|description" lines (see framework/runner/src/main.cpp's listTests())
# into a JSON array. Splits on only the first two '|'s, not every '|' in the
# line -- a description containing one of its own (unlikely, but nothing
# stops it) would otherwise get truncated at the wrong point.
#
set(THORIUM_TESTS_JSON "")
foreach(line IN LISTS THORIUM_CATALOG_LINES)
    if(line STREQUAL "")
        continue()
    endif()

    string(FIND "${line}" "|" first_pipe)
    if(first_pipe EQUAL -1)
        continue()
    endif()
    string(SUBSTRING "${line}" 0 ${first_pipe} group)
    math(EXPR after_first "${first_pipe} + 1")
    string(SUBSTRING "${line}" ${after_first} -1 rest)

    string(FIND "${rest}" "|" second_pipe)
    if(second_pipe EQUAL -1)
        continue()
    endif()
    string(SUBSTRING "${rest}" 0 ${second_pipe} id)
    math(EXPR after_second "${second_pipe} + 1")
    string(SUBSTRING "${rest}" ${after_second} -1 description)

    # Minimal JSON-string escaping -- backslash and quote only, since group/
    # id/description are plain prose out of suite/test_catalog.inc, never
    # arbitrary or untrusted input.
    foreach(field group id description)
        string(REPLACE "\\" "\\\\" ${field} "${${field}}")
        string(REPLACE "\"" "\\\"" ${field} "${${field}}")
    endforeach()

    if(NOT THORIUM_TESTS_JSON STREQUAL "")
        string(APPEND THORIUM_TESTS_JSON ",")
    endif()
    string(APPEND THORIUM_TESTS_JSON
        "\n    { \"group\": \"${group}\", \"id\": \"${id}\", \"description\": \"${description}\" }")
endforeach()

set(THORIUM_VARIANTS_JSON "")
foreach(variant IN LISTS THORIUM_KNOWN_CRITERIA_VARIANTS)
    if(NOT THORIUM_VARIANTS_JSON STREQUAL "")
        string(APPEND THORIUM_VARIANTS_JSON ", ")
    endif()
    string(APPEND THORIUM_VARIANTS_JSON "\"${variant}\"")
endforeach()

file(WRITE "${THORIUM_MANIFEST_OUTPUT}" "{
  \"criteriaVariants\": [${THORIUM_VARIANTS_JSON}],
  \"defaultCriteriaVariant\": \"${THORIUM_CRITERIA_VARIANT}\",
  \"binary\": \"run_scripts\",
  \"tests\": [${THORIUM_TESTS_JSON}
  ]
}
")

message(STATUS "Wrote manifest: ${THORIUM_MANIFEST_OUTPUT}")
