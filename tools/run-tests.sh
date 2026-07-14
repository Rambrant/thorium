#!/usr/bin/env bash
#
# Interactive picker for Thorium's test and run executables. Written for
# bash 3.2 (macOS's default /bin/bash) as well as newer bash -- no
# mapfile/readarray, associative arrays, or other bash-4-only features.
#
# Usage: tools/run-tests.sh [build-dir]
# If build-dir is omitted, tries ./build, ./cmake-build-debug,
# ./cmake-build-release, in that order.
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

# --- Known executables, relative to $BUILD_DIR -----------------------------
# A case statement instead of an array keeps this portable to bash 3.2,
# which has indexed arrays but not associative ones.
path_for()
{
    case "$1" in
        core_tests)    echo "libs/core/core_tests" ;;
        hal_tests)     echo "libs/hal/hal_tests" ;;
        dut_tests)     echo "libs/dut/dut_tests" ;;
        dsl_tests)     echo "libs/dsl/dsl_tests" ;;
        scripts_tests) echo "app/scripts_tests" ;;
        run_scripts)   echo "app/run_scripts" ;;
        *)             echo "" ;;
    esac
}

echo "Build dir: $BUILD_DIR"
echo "Which executable?"
choice=""
select opt in core_tests hal_tests dut_tests dsl_tests scripts_tests run_scripts "All tests (ctest)" "Quit"; do
    case "$opt" in
        "Quit")
            exit 0
            ;;
        "All tests (ctest)")
            exec ctest --test-dir "$BUILD_DIR" --output-on-failure
            ;;
        "")
            echo "Not a valid choice, try again."
            ;;
        *)
            choice="$opt"
            break
            ;;
    esac
done

BIN="$BUILD_DIR/$(path_for "$choice")"
if [ ! -x "$BIN" ]; then
    echo "Executable not found or not built yet: $BIN" >&2
    echo "Build it first, e.g.: cmake --build \"$BUILD_DIR\" --target $choice" >&2
    exit 1
fi

if [ "$choice" = "run_scripts" ]; then
    # --- Catalog-aware picker -----------------------------------------------
    # Expects run_scripts to support:
    #   --list-tests        print "group|id|description", one line per test
    #   --select=id1,id2    run only those test ids (blank/absent = run all)
    # Older binaries without --list-tests just fall through to running
    # everything, same as before.
    catalog="$("$BIN" --list-tests 2>/dev/null || true)"
    if [ -z "$catalog" ]; then
        echo "Running $BIN ..."
        exec "$BIN"
    fi

    echo
    echo "Groups in $choice:"
    groups="$(printf '%s\n' "$catalog" | awk -F'|' '!seen[$1]++ { print $1 }')"

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

    echo
    echo "Tests in group '$group':"
    printf '%s\n' "$catalog" | awk -F'|' -v grp="$group" '$1 == grp { printf "  %s - %s\n", $2, $3 }'
    echo

    echo "Enter comma-separated test ids to run (blank = run all in this group):"
    read -r ids
    if [ -z "$ids" ]; then
        ids="$(printf '%s\n' "$catalog" | awk -F'|' -v grp="$group" '$1 == grp { printf "%s,", $2 }' | sed 's/,$//')"
    fi

    echo "Running $BIN --select=$ids ..."
    exec "$BIN" "--select=$ids"
fi

# --- A GoogleTest binary: show what's in it, then let the user filter -----
echo
echo "Tests in $choice:"
"$BIN" --gtest_list_tests
echo

echo "Enter a --gtest_filter pattern (blank = run all, e.g. 'CoreQuantity.*'):"
read -r pattern
if [ -z "$pattern" ]; then
    exec "$BIN"
else
    exec "$BIN" --gtest_filter="$pattern"
fi
