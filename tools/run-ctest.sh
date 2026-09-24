#!/usr/bin/env bash
#
# Runs every ctest suite in this repository and refuses to report success while
# one of them is missing.
#
# Usage: tools/run-ctest.sh [build-dir ...] [-- <extra ctest args>]
#
# With no build directory named, every tree this repository knows how to make is
# run if it has been configured -- the deployment presets (build/debug,
# build/dev, build/release) and the launcher's (build/launcher). Name one or
# more directories to run just those.
#
# ---------------------------------------------------------------------------
# Why this exists at all
# ---------------------------------------------------------------------------
# One framework source tree produces several build trees -- a preset per
# deployment and build type, each compiling different suite content against the
# same framework.
#
# ctest reads exactly one build tree. So `ctest --test-dir build/debug` reports
# one deployment's tests and silently knows nothing about the others.
#
# That is a fine arrangement and a terrible default, because the failure is
# invisible: a developer runs "all the tests", sees a four-digit number and a
# green line, and has tested a fraction of the repository. An IDE makes it worse
# -- a CLion "Run all CTests" is per build profile, so it can only ever show one.
#
# So this script is the front door, and the load-bearing part is not the
# convenience of one command. It is the summary at the end, which names every
# tree it ran AND every tree it did not, so "everything passed" can never
# quietly mean "the half I had configured passed".
#
# Deliberately separate from tools/run-tests.sh, which is a bench operator's
# catalog picker and explains at length why unit tests have no place in it.
# This is a developer's script and touches no DUT.
#
# What it deliberately does NOT do: pass --no-tests=error. ctest exits 0 when a
# -R filter matches nothing, which looks like exactly the silent-omission
# problem above and is not the same thing -- a filter aimed at one deployment's
# tests legitimately matches nothing in another's. Turning that into an error
# would break every filtered run to catch a case the caller created on purpose.
# The absence this script guards against is a build tree that was never made,
# which no filter can explain away.
#
# There used to be a second kind of tree here, framework/ui's own
# `build/ui` -- a separate CMake project, because wxWidgets could not be built
# by this framework's compiler on macOS. It is gone along with that directory
# (see framework/webui/README.md's "History"): framework/webui joined the
# ordinary build, so its tests are already inside build/debug, build/dev and
# build/release like any other layer's, and this script needed no console-
# specific case once that was true.
#
# build/launcher is the one separate project left: framework/launcher, the tray
# or menu bar program that starts the console. It is separate because it holds
# no framework code, not because of a compiler -- on macOS it is GCC 16 like
# everything else (see framework/launcher/README.md) -- and its tests would be
# exactly the kind this script exists to stop being forgotten, so it is listed
# below with the rest.
#
set -uo pipefail

#
# Run from the repository root whatever the caller's working directory was.
#
# Every path below is relative to the root, and the callers who most need this
# script are the ones least likely to be standing in the right place: an IDE run
# configuration, a CI step, a shell that happens to be in some other directory.
#
cd "$( dirname "$0")/.." || exit 1

#
# Every tree this repository knows how to produce.
#
# cmake-build-debug is not here on purpose. It is whatever an IDE made, it is
# not produced by a preset, and a profile that names no compiler configures with
# the wrong one (see README.md's note on CLion). Naming it explicitly still
# works; including it in a "run everything" default would mean this script's
# result depended on an IDE's settings.
#
KNOWN_TREES="build/debug build/dev build/release build/launcher"

# --- Arguments -------------------------------------------------------------
#
# The build directories are optional and the separator is optional, so each
# argument has to be examined rather than assumed. Taking "$1" as a directory
# unconditionally is what the first version did, and it turned
# `run-ctest.sh -- --output-on-failure` -- the form the usage line documents --
# into a search for a build tree called "--".
#
REQUESTED=""

while [ $# -gt 0 ] && [ "$1" != "--" ]; do
    REQUESTED="$REQUESTED $1"
    shift
done

if [ "${1:-}" = "--" ]; then
    shift
fi

# --- Which trees to run ----------------------------------------------------
CTEST_PARALLEL=""
if [[ "$*" != *"-j"* ]]; then
    CORES=2
    if command -v nproc >/dev/null 2>&1; then
        CORES=$(nproc)
    elif command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu >/dev/null 2>&1; then
        CORES=$(sysctl -n hw.ncpu)
    fi
    CTEST_PARALLEL="-j $CORES"
fi

TO_RUN=""
SKIPPED=""

if [ -n "$REQUESTED" ]; then
    #
    # An explicitly named tree that is not configured is an error, not a skip:
    # the caller asked for it by name, so silently not running it would answer a
    # different question from the one they asked.
    #
    for tree in $REQUESTED; do
        if [ ! -f "${tree}/CTestTestfile.cmake" ]; then
            echo "Not a configured build tree: ${tree}" >&2
            exit 1
        fi

        TO_RUN="$TO_RUN $tree"
    done
else
    for tree in $KNOWN_TREES; do
        if [ -f "${tree}/CTestTestfile.cmake" ]; then
            TO_RUN="$TO_RUN $tree"
        else
            SKIPPED="$SKIPPED $tree"
        fi
    done
fi

if [ -z "$TO_RUN" ]; then
    echo "No configured build tree found." >&2
    echo "  cmake --preset macos-debug && cmake --build build/debug" >&2
    exit 1
fi

# --- Run them --------------------------------------------------------------
status=0
results=""

for tree in $TO_RUN; do
    echo
    echo "=== ${tree} ==="

    if ctest --test-dir "$tree" $CTEST_PARALLEL "$@"; then
        results="${results}  PASS  ${tree}"$'\n'
    else
        results="${results}  FAIL  ${tree}"$'\n'
        status=1
    fi
done

# --- Say what ran, and what did not ----------------------------------------
echo
echo "--- summary ---"
printf '%s' "$results"

for tree in $SKIPPED; do
    echo "  ----  ${tree} (not configured)"
done

#
# build/debug and build/dev are two *deployments* of one framework --
# configuring one and not the other is a legitimate choice, and neither is a
# part of the repository the other leaves untested. That is why a skipped tree
# above is a note, not a failure: every configured tree already ran. The one
# exception in kind is build/launcher, a second CMake project -- but it is in
# KNOWN_TREES, so an unconfigured one is still named here rather than
# silently absent.
#

echo

if [ "$status" -eq 0 ]; then
    echo "All suites passed."
else
    echo "Not a clean run -- see the summary above." >&2
fi

exit "$status"
