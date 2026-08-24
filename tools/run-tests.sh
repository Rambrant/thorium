#!/usr/bin/env bash
#
# Interactive picker for this suite's test catalog: pick a group from a menu,
# then pick one, several, or all of that group's tests from a second menu, and
# run them against the DUT. Written for bash 3.2 (macOS's default /bin/bash) as
# well as newer bash -- no mapfile/readarray, associative arrays, or other
# bash-4-only features.
#
# Usage: tools/run-tests.sh [build-dir]
# If build-dir is omitted, tries ./build, ./cmake-build-debug,
# ./cmake-build-release, in that order.
#
# ---------------------------------------------------------------------------
# What this deliberately does NOT offer
# ---------------------------------------------------------------------------
# The catalog, and nothing else. This script used to also list the per-layer
# unit-test binaries (core_tests, hal_tests, dut_tests, dsl_tests,
# scripts_tests) and an "all tests via ctest" option, and that was a category
# error: those are build artifacts, verified by the build system, and they say
# nothing about the DUT sitting in the fixture. Whoever is running this script
# is testing a *device*; the framework's own correctness is not their question,
# and offering it to them invites a failing unit test to be read as a failing
# DUT (or worse, the other way round).
#
# So there is exactly one binary reachable from here -- run_scripts, this
# suite's catalog runner (see app/src/main.cpp) -- and the only choices are
# which of its catalog tests to run. Unit tests, and the run_scripts CLI
# acceptance tests (app/tests/test_acceptance.cpp), belong to `ctest`:
#
#     ctest --test-dir <build-dir> --output-on-failure
#
# The acceptance tests carry a label of their own, since they drive the real
# runner and are the slow half:
#
#     ctest --test-dir <build-dir> -L  acceptance   # only those
#     ctest --test-dir <build-dir> -LE acceptance   # everything else
#
# Nothing here should grow a way to invoke them. If a tester needs a
# framework-level check run, that is a developer's job on a developer's build,
# not a menu entry on a bench.
#
set -eu

# --- Locate the build directory -------------------------------------------
BUILD_DIR="${1:-${BUILD_DIR:-}}"
if [ -z "$BUILD_DIR" ]; then
    for candidate in build cmake-build-debug cmake-build-release; do
        if [ -d "$candidate" ]; then
            BUILD_DIR="$candidate"
            break
        fi
    done
fi
if [ -z "$BUILD_DIR" ] || [ ! -d "$BUILD_DIR" ]; then
    echo "Could not find a build directory. Usage: tools/run-tests.sh [build-dir]" >&2
    exit 1
fi

# --- The one binary this script drives -------------------------------------
# Hardcoded rather than looked up from a table of choices: there is only one,
# by design -- see this file's own comment above.
BIN="$BUILD_DIR/app/run_scripts"

if [ ! -x "$BIN" ]; then
    echo "Suite runner not found or not built yet: $BIN" >&2
    echo "Build it first, e.g.: cmake --build \"$BUILD_DIR\" --target run_scripts" >&2
    exit 1
fi

echo "Build dir: $BUILD_DIR"

# --- Catalog ---------------------------------------------------------------
# Expects run_scripts to support:
#   --list-tests        print "group|id|description", one line per test
#   --select=id1,id2    run only those test ids (blank/absent = run all)
# Older binaries without --list-tests just fall through to running everything:
# with no catalog to read there is nothing to pick from, so there is nothing
# this script can usefully offer beyond the whole suite.
catalog="$("$BIN" --list-tests 2>/dev/null || true)"
if [ -z "$catalog" ]; then
    echo "Running $BIN ..."
    exec "$BIN"
fi

echo
echo "Groups in this suite:"
groups="$(printf '%s\n' "$catalog" | awk -F'|' '!seen[$1]++ { print $1 }')"

# Newline-delimited into the positional parameters, so `select` below gets one
# entry per group -- the bash 3.2-portable stand-in for an array. Safe to
# clobber $@ here: the only argument this script takes is the build directory,
# already captured above.
old_ifs="$IFS"
IFS='
'
set -- $groups
IFS="$old_ifs"

echo "Select a group:"
group=""
select g in "$@" "All groups" "Quit"; do
    case "$g" in
        "Quit")       exit 0 ;;
        "All groups") group=""; break ;;
        "")           echo "Not a valid choice, try again." ;;
        *)            group="$g"; break ;;
    esac
done

if [ -z "$group" ]; then
    echo "Running all tests in $BIN ..."
    exec "$BIN"
fi

# --- Individual tests within the chosen group ------------------------------
# Numbered and selectable, the same way the group menu above is -- but built by
# hand rather than with `select`, because this step is genuinely multi-choice
# and `select` only ever yields one answer. Running two of a group's four tests
# is an ordinary thing to want at a bench, so the reply is parsed as a list.
#
# Test ids are still accepted alongside the numbers, for anyone who knows the
# one they want. That is not just muscle memory: an id typed here is checked
# against the group before the run starts, where a typo passed straight to
# --select would simply mean that test doesn't run (see app/src/main.cpp on why
# the runner deliberately doesn't treat it as an error) -- a silent no-op is
# much worse news to a tester than a re-prompt.
#
# Telling the two apart by "is it all digits" is sound rather than a guess: a
# test id is a C++ identifier (it comes from a TEST(...) macro token, see
# core/test_catalog.hpp), so it can neither start with a digit nor consist only
# of them. No id can ever be shadowed by a menu number.
#
# Selecting the same test twice is harmless and needs no de-duplication: the
# runner walks the catalog and asks whether each test was selected, rather than
# walking the selection, so a repeated id still runs exactly once.
#
# "id|description" per entry, so the menu can show both while the id stays
# recoverable with a prefix strip -- neither field can contain a '|', since both
# come from a TEST(...) macro's own tokens (see core/test_catalog.hpp).
tests="$(printf '%s\n' "$catalog" | awk -F'|' -v grp="$group" '$1 == grp { print $2 "|" $3 }')"

old_ifs="$IFS"
IFS='
'
set -- $tests
IFS="$old_ifs"

test_count=$#

echo
echo "Tests in group '$group':"

n=1
for entry in "$@"; do
    printf '%2d) %-16s %s\n' "$n" "${entry%%|*}" "${entry#*|}"
    n=$((n + 1))
done

printf ' a) All tests in this group\n'
printf ' q) Quit\n'
echo

# Re-prompts rather than bailing on a bad reply: this runs on a bench, and
# making somebody restart the whole picker over a mistyped number is no way to
# treat them.
ids=""
while :; do
    printf 'Select tests (numbers and/or ids, space- or comma-separated): '
    read -r reply || exit 0

    case "$reply" in
        q|Q|quit|Quit)
            exit 0
            ;;
        ""|a|A|all|All)
            # Blank still means "all in this group", as it always has.
            ids="$(printf '%s\n' "$catalog" | awk -F'|' -v grp="$group" '$1 == grp { printf "%s,", $2 }' | sed 's/,$//')"
            break
            ;;
    esac

    selection=""
    invalid=""

    # Commas to spaces, so "1,3" and "1 3" are the same reply -- word splitting
    # then does the rest, which is the bash 3.2-portable way to walk a list.
    for token in $(printf '%s' "$reply" | tr ',' ' '); do
        id=""

        case "$token" in
            *[!0-9]*)
                # Not a number, so treat it as a test id -- accepted only if
                # this group actually has one by that name.
                for entry in "$@"; do
                    if [ "${entry%%|*}" = "$token" ]; then
                        id="${entry%%|*}"
                        break
                    fi
                done
                ;;
            *)
                if [ "$token" -ge 1 ] && [ "$token" -le "$test_count" ]; then
                    # Indirect positional expansion -- $token is known to be a
                    # plain in-range number by the time this runs.
                    eval "entry=\${$token}"
                    id="${entry%%|*}"
                fi
                ;;
        esac

        if [ -z "$id" ]; then
            invalid="$invalid $token"
        else
            selection="$selection${selection:+,}$id"
        fi
    done

    if [ -n "$invalid" ]; then
        echo "Not a test in this group:$invalid" >&2
        continue
    fi

    if [ -n "$selection" ]; then
        ids="$selection"
        break
    fi
done

echo "Running $BIN --select=$ids ..."
exec "$BIN" "--select=$ids"
