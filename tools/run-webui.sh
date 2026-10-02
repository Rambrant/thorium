#!/usr/bin/env bash
#
# Starts the bench console: thorium_webui in front of a run_scripts, either
# behind framework/launcher (menu-bar or tray icon, Chrome app window) or
# on its own with the page opened in the default browser.
#
# macOS and Windows both. On Windows it wants Git Bash, like every other script
# under tools/ (see README.md's "Tests"), rather than a .ps1 copy to keep in
# step with this one.
#
# Usage: tools/run-webui.sh [--tree=NAME] [--run-scripts=PATH] [--webui=PATH]
#                           [--port=N] [--no-launcher]
#
#   --tree=NAME         the build tree to take all three binaries from:
#                       build/NAME (dev, debug or release). Default: the one
#                       whose run_scripts was built most recently
#   --run-scripts=PATH  the run_scripts to drive instead, e.g. an installed
#                       one: build/install/bin/run_scripts
#   --webui=PATH        the thorium_webui server to start instead; the
#                       launcher is taken from beside it
#   --port=N            the loopback port the server listens on (default: 8420)
#   --no-launcher       run the server in this terminal and open the page in
#                       the default browser instead; Ctrl-C stops it. The
#                       default wherever there is no launcher built.
#
# Nothing here decides anything framework/launcher or thorium_webui do not
# already decide -- it only finds the three binaries and hands them the flags
# their READMEs document, so a developer does not have to retype two absolute
# paths every time. Runs started from the console write their logs to
# build/logs.
#
# A build tree's own run_scripts is the default, not an installed one: an
# install only changes on `cmake --install`, so a console driving it kept
# showing yesterday's suite after every edit and rebuild. Nothing needs the
# install any more -- the criteria picker asks run_scripts itself
# (--describe-criteria), where it used to need the install's manifest.json.
#
# The trees are different deployments -- build/dev is the dev rig and DUT,
# build/debug and build/release the real ones -- so "the newest" decides which
# bench the console drives. It is printed before anything starts; --tree=
# pins it.
#
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# --- What differs between the two hosts ---
# Everything platform-specific is here, so the rest of the script reads the
# same on both: the executable suffix, where the launcher sits in a build
# tree, what its icon is called, and three tools Git Bash does not ship -- lsof, pgrep and
# open -- replaced by the Windows commands that answer the same questions.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) windows=1 ;;
    *)                    windows=0 ;;
esac

if [[ $windows -eq 1 ]]; then
    exe=".exe"
    launcher_path="framework/launcher/thorium_launcher.exe"
    icon="tray icon"

    # netstat's local-address column is "127.0.0.1:8420" or "[::]:8420"; the
    # trailing space keeps port 842 from matching 8420.
    port_in_use() { netstat -ano | grep "LISTENING" | grep -q ":$1 "; }
    is_running()  { tasklist //FI "IMAGENAME eq $1.exe" //NH 2>/dev/null | grep -qi "^$1.exe"; }
    # Chrome in app mode, found the way framework/launcher's findChrome()
    # finds it -- the App Paths key, per-user then per-machine -- so that
    # running without the launcher still gives the console Chrome and its own
    # profile, not whatever the system default browser is (Edge, on a fresh
    # Windows). The default browser only if there is no Chrome at all.
    find_chrome() {
        local key='SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\chrome.exe'
        local root path
        for root in HKCU HKLM; do
            path="$(reg query "$root"'\'"$key" //ve 2>/dev/null | sed -n 's/.*REG_SZ[[:space:]]*//p' | tr -d '\r')"
            if [[ -n "$path" && -f "$(cygpath -u "$path")" ]]; then
                printf '%s\n' "$path"
                return 0
            fi
        done
        return 1
    }
    open_url() {
        local chrome
        if chrome="$(find_chrome)"; then
            "$(cygpath -u "$chrome")" --app="$1" \
                --user-data-dir="$(cygpath -w "$LOCALAPPDATA")\\Thorium\\chrome-profile" \
                --window-size=1024,768 --no-first-run --no-default-browser-check >/dev/null 2>&1 &
        else
            echo "Chrome not found -- opening the default browser instead." >&2
            cmd.exe //c start "" "$1"
        fi
    }
    stop_hint="taskkill //IM thorium_webui.exe //F"

    # The binaries are native Windows programs, so they get C:/dev/... rather
    # than Git Bash's /c/dev/..., which only this shell understands. MSYS
    # converts a bare path argument by itself, but not reliably one embedded in
    # --flag=VALUE, and the launcher passes these on to a child process again.
    native_path() { cygpath -m "$1"; }
else
    exe=""
    launcher_path="framework/launcher/macos/thorium_launcher"
    icon="menu-bar icon"

    port_in_use() { lsof -nP -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1; }
    is_running()  { pgrep -f "$1" >/dev/null 2>&1; }
    open_url() {
        if command -v open >/dev/null; then
            open "$1"
        elif command -v xdg-open >/dev/null; then
            xdg-open "$1" >/dev/null 2>&1
        fi
    }
    stop_hint="pkill -f thorium_webui"
    native_path() { printf '%s\n' "$1"; }
fi

run_scripts=""
tree=""
webui=""
port=8420
use_launcher=1

for arg in "$@"; do
    case "$arg" in
        --tree=*)        tree="${arg#*=}" ;;
        --run-scripts=*) run_scripts="${arg#*=}" ;;
        --webui=*)       webui="${arg#*=}" ;;
        --port=*)        port="${arg#*=}" ;;
        --no-launcher)   use_launcher=0 ;;
        -h|--help)       sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)               echo "Unrecognised argument: $arg (see --help)" >&2; exit 2 ;;
    esac
done

# The newest rather than the first of a fixed order: a tree that has not been
# built for days must not win over the one just rebuilt, or the console is
# quietly the old one -- which is exactly what a fixed dev-first order did
# while CLion was rebuilding build/release. Judged by run_scripts, the binary
# a suite edit changes. -nt compares modification times, and is a bash
# builtin on both hosts, unlike stat, whose flags differ between them.
newest() {   # newest <path inside a tree>: the tree name, or nothing
    local best="" name
    for name in dev release debug; do
        if [[ -x "$repo/build/$name/$1" && ( -z "$best" || "$repo/build/$name/$1" -nt "$repo/build/$best/$1" ) ]]; then
            best="$name"
        fi
    done
    printf '%s\n' "$best"
}

if [[ -z "$tree" && -z "$run_scripts" ]]; then
    tree="$(newest "bin/run_scripts$exe")"
fi
if [[ -z "$tree" && -z "$webui" ]]; then
    tree="$(newest "framework/webui/thorium_webui$exe")"
fi
if [[ -n "$tree" && ! -d "$repo/build/$tree" ]]; then
    echo "No build tree build/$tree -- configure and build it first (cmake --list-presets)" >&2
    exit 1
fi

# Whether run_scripts is the tree's own, which is what makes the tree's
# deployment the one being driven -- see the deployment line below.
own_run_scripts=0
if [[ -z "$run_scripts" ]]; then
    run_scripts="$repo/build/$tree/bin/run_scripts$exe"
    own_run_scripts=1
fi
[[ -n "$webui" ]]       || webui="$repo/build/$tree/framework/webui/thorium_webui$exe"

if [[ ! -x "$webui" ]]; then
    echo "No thorium_webui at $webui -- build it first, e.g.: cmake --build build/${tree:-dev}" >&2
    exit 1
fi

if [[ ! -x "$run_scripts" ]]; then
    echo "No run_scripts at $run_scripts -- build it first, e.g.: cmake --build build/${tree:-dev}" >&2
    exit 1
fi

# Absolute, because the launcher hands these to a child process and neither it
# nor the server promises to keep this script's working directory.
webui="$(native_path "$(cd "$(dirname "$webui")" && pwd)/$(basename "$webui")")"
run_scripts="$(native_path "$(cd "$(dirname "$run_scripts")" && pwd)/$(basename "$run_scripts")")"

# Refused up front rather than left to the server: a second console on the
# same port would fail to bind, and behind the launcher that failure is easy
# to miss -- the window opens onto whichever server already has the port.
# Named for its likeliest cause, because that cause is invisible: a console
# whose window was closed during a run keeps going until the run ends, and
# all that shows of it is the menu-bar or tray icon.
if port_in_use "$port"; then
    if is_running thorium_launcher; then
        echo "A bench console is already running (port $port is taken)." >&2
        echo "Use its $icon: Show console to bring the window back, or Quit to stop it." >&2
    elif is_running thorium_webui; then
        echo "A console server is already running on port $port, without the launcher." >&2
        echo "Open http://127.0.0.1:$port/ to use it, or stop it with: $stop_hint" >&2
    else
        echo "Port $port is in use by another program -- pass --port=N to use a different one." >&2
    fi
    exit 1
fi

# The launcher from the same build tree as the server -- <tree>/framework/
# webui/thorium_webui -- since the two are built together (framework/launcher
# is part of the ordinary build): a launcher from another tree would be one
# built on another day.
webui_tree="$(dirname "$(dirname "$(dirname "$webui")")")"
launcher="$webui_tree/$launcher_path"

if [[ $use_launcher -eq 1 && ! -x "$launcher" ]]; then
    echo "No launcher built at $launcher -- running the server on its own." >&2
    echo "(It is built with the server: cmake --build $webui_tree)" >&2
    use_launcher=0
fi

# Started from build/, so a run's logs land in build/logs rather than in
# whatever directory this script happened to be run from -- run_scripts'
# --log-dir defaults to "logs", relative to its working directory, which it
# inherits from the server, which inherits it from the launcher, which
# inherits it from here. Neither of those changes directory, so this one cd
# is the whole mechanism; every path above is already absolute.
mkdir -p "$repo/build/logs"
cd "$repo/build"

# Which rig and DUT this is, from the tree's own configure -- the one line
# that says which bench the console is about to drive.
if [[ $own_run_scripts -eq 1 && -f "$repo/build/$tree/CMakeCache.txt" ]]; then
    rig_dir="$(sed -n 's/^THORIUM_RIG_DIR:[A-Z]*=//p' "$repo/build/$tree/CMakeCache.txt")"
    echo "deployment:    build/$tree (rig: ${rig_dir#"$repo"/})"
fi
echo "thorium_webui: $webui"
echo "run_scripts:   $run_scripts"
echo "console:       http://127.0.0.1:$port/"
echo "logs:          $(native_path "$repo/build/logs")"

if [[ $use_launcher -eq 1 ]]; then
    # exec, so a Ctrl-C or a kill reaches the launcher itself, which owns the
    # server's lifetime (see framework/launcher/README.md).
    exec "$launcher" --server="$webui" \
        --server-arg=--run-scripts="$run_scripts" \
        --server-arg=--port="$port" \
        --port="$port"
fi

"$webui" --run-scripts="$run_scripts" --port="$port" &
server=$!
trap 'kill "$server" 2>/dev/null; wait "$server" 2>/dev/null' EXIT INT TERM

# Opened only once the server answers, so the first page load is not a
# connection error.
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:$port/"; then
        open_url "http://127.0.0.1:$port/"
        break
    fi
    kill -0 "$server" 2>/dev/null || break
    sleep 0.1
done

wait "$server"
