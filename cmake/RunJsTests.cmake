#
# Runs at test time, as ctest's webui_js_tests (see framework/webui/CMakeLists.txt)
# -- opens framework/webui/tests/js/run.html in headless Chrome, reads the report
# the page wrote into itself back out of --dump-dom, prints it, and fails unless
# its last line says every test passed.
#
# Chrome rather than Node: Chrome is already on every machine that runs the
# console (framework/launcher needs it), and this way the page's modules are
# tested in the engine that runs them. Nothing new to install is the point --
# which is also why this is a CMake script and not a shell one: ctest runs it
# the same on both hosts, with no Git Bash in between.
#
# THORIUM_CHROME       -- the Chrome executable; empty when configure found none
# JS_TEST_PAGE         -- run.html, absolute
# JS_TEST_PROFILE_DIR  -- where each run's scratch --user-data-dir goes, so a
#                         Chrome the developer has open is neither attached to
#                         nor disturbed
#
if(NOT THORIUM_CHROME)
    # A failure, not a skip: tools/run-ctest.sh's whole argument is that a
    # suite that quietly did not run must never read as one that passed.
    message(FATAL_ERROR
        "No Chrome found at configure time, so the console's JavaScript tests cannot run. "
        "Install Google Chrome, or point THORIUM_CHROME at one and reconfigure: "
        "cmake -DTHORIUM_CHROME=/path/to/chrome <build-dir>")
endif()
foreach(required JS_TEST_PAGE JS_TEST_PROFILE_DIR)
    if(NOT ${required})
        message(FATAL_ERROR "${required} is empty or unset -- pass it with -D")
    endif()
endforeach()

# file:///C:/... on Windows, file:///Users/... elsewhere.
if(JS_TEST_PAGE MATCHES "^/")
    set(url "file://${JS_TEST_PAGE}")
else()
    set(url "file:///${JS_TEST_PAGE}")
endif()

# A profile of this run's own, under JS_TEST_PROFILE_DIR, and gone afterwards.
# One shared profile is one Chrome at a time: a second run in the same tree --
# CLion's and a terminal's ctest together, or one started while the last
# headless Chrome is still exiting -- found its SingletonLock and Chrome
# aborted (exit 21) rather than share it, which read as the tests failing.
# A fresh one also means no cache from an earlier run can stand in for the
# modules as they are now.
string(RANDOM LENGTH 12 run_id)
set(profile "${JS_TEST_PROFILE_DIR}/run-${run_id}")
file(MAKE_DIRECTORY "${profile}")

# --allow-file-access-from-files, because a page opened from file:// may not
# otherwise import modules at all: its origin is opaque, and every import is a
# cross-origin fetch. --virtual-time-budget lets the page's own async work
# (all.js awaits run()) finish before the DOM is dumped, without a real wait.
execute_process(
    COMMAND "${THORIUM_CHROME}"
            --headless
            --disable-gpu
            --no-first-run
            --no-default-browser-check
            --allow-file-access-from-files
            "--user-data-dir=${profile}"
            --virtual-time-budget=10000
            --dump-dom
            "${url}"
    OUTPUT_VARIABLE dom
    ERROR_VARIABLE chrome_stderr
    RESULT_VARIABLE chrome_result
    TIMEOUT 120
)
file(REMOVE_RECURSE "${profile}")

# The two <pre>s run.html has: the report, and whatever failed to load.
function(extract_pre id out)
    set(open "<pre id=\"${id}\">")
    string(FIND "${dom}" "${open}" start)
    if(start EQUAL -1)
        set(${out} "" PARENT_SCOPE)
        return()
    endif()
    string(LENGTH "${open}" open_length)
    math(EXPR start "${start} + ${open_length}")
    string(SUBSTRING "${dom}" ${start} -1 rest)
    string(FIND "${rest}" "</pre>" end)
    string(SUBSTRING "${rest}" 0 ${end} text)
    # --dump-dom serialises text, so these four are all it can have escaped.
    string(REPLACE "&lt;" "<" text "${text}")
    string(REPLACE "&gt;" ">" text "${text}")
    string(REPLACE "&quot;" "\"" text "${text}")
    string(REPLACE "&amp;" "&" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

extract_pre(report report)
extract_pre(errors errors)

if(report)
    message("${report}")
endif()
if(errors)
    message("Errors on the test page:\n${errors}")
endif()

if(NOT report MATCHES "# result: PASS$")
    if(NOT report)
        message("The test page wrote no report -- a module failed to load, or Chrome did not open it.")
        message("Chrome exited with: ${chrome_result}")
        if(chrome_stderr)
            message("Chrome's stderr:\n${chrome_stderr}")
        endif()
    endif()
    message(FATAL_ERROR "The console's JavaScript tests failed.")
endif()
