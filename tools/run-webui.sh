#!/usr/bin/env bash
#
# Starts the bench console: thorium_webui in front of an installed run_scripts,
# either behind framework/launcher (menu-bar icon, Chrome app window) or on its
# own with the page opened in the default browser.
#
# Usage: tools/run-webui.sh [--run-scripts=PATH] [--webui=PATH] [--port=N]
#                           [--no-launcher]
#
#   --run-scripts=PATH  the run_scripts to drive
#                       (default: build/install/bin/run_scripts)
#   --webui=PATH        the thorium_webui server to start (default: the first
#                       one built, of build/dev, build/release, build/debug)
#   --port=N            the loopback port the server listens on (default: 8420)
#   --no-launcher       run the server in this terminal and open the page in
#                       the default browser instead; Ctrl-C stops it. The
#                       default wherever there is no launcher built.
#
# Nothing here decides anything framework/launcher or thorium_webui do not
# already decide -- it only finds the three binaries and hands them the flags
# their READMEs document, so a developer does not have to retype two absolute
# paths every time. Runs started from the console write their logs to
# build/logs. An installed run_scripts is the default rather than a
# build-tree one because only an install has the manifest.json beside it that
# the page's criteria picker is built from (see framework/webui/README.md).
#
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

run_scripts="$repo/build/install/bin/run_scripts"
webui=""
port=8420
use_launcher=1

for arg in "$@"; do
    case "$arg" in
        --run-scripts=*) run_scripts="${arg#*=}" ;;
        --webui=*)       webui="${arg#*=}" ;;
        --port=*)        port="${arg#*=}" ;;
        --no-launcher)   use_launcher=0 ;;
        -h|--help)       sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)               echo "Unrecognised argument: $arg (see --help)" >&2; exit 2 ;;
    esac
done

if [[ -z "$webui" ]]; then
    for tree in dev release debug; do
        candidate="$repo/build/$tree/framework/webui/thorium_webui"
        if [[ -x "$candidate" ]]; then
            webui="$candidate"
            break
        fi
    done
fi

if [[ -z "$webui" || ! -x "$webui" ]]; then
    echo "No thorium_webui found -- build one first, e.g.: cmake --build build/dev" >&2
    exit 1
fi

if [[ ! -x "$run_scripts" ]]; then
    echo "No run_scripts at $run_scripts -- install one, e.g.:" >&2
    echo "  cmake --install build/release --prefix build/install" >&2
    exit 1
fi

# Absolute, because the launcher hands these to a child process and neither it
# nor the server promises to keep this script's working directory.
webui="$(cd "$(dirname "$webui")" && pwd)/$(basename "$webui")"
run_scripts="$(cd "$(dirname "$run_scripts")" && pwd)/$(basename "$run_scripts")"

[[ -f "$(dirname "$run_scripts")/manifest.json" ]] ||
    echo "Note: no manifest.json beside $run_scripts -- the criteria picker will be a text field." >&2

# Refused up front rather than left to the server: a second console on the
# same port would fail to bind, and behind the launcher that failure is easy
# to miss -- the window opens onto whichever server already has the port.
# Named for its likeliest cause, because that cause is invisible: a console
# whose window was closed during a run keeps going until the run ends, and
# all that shows of it is the menu-bar icon.
if lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then
    if pgrep -f "thorium_launcher --server=" >/dev/null 2>&1; then
        echo "A bench console is already running (port $port is taken)." >&2
        echo "Use its menu-bar icon: Show console to bring the window back, or Quit to stop it." >&2
    elif pgrep -f "thorium_webui --run-scripts=" >/dev/null 2>&1; then
        echo "A console server is already running on port $port, without the launcher." >&2
        echo "Open http://127.0.0.1:$port/ to use it, or stop it with: pkill -f thorium_webui" >&2
    else
        echo "Port $port is in use by another program -- pass --port=N to use a different one." >&2
    fi
    exit 1
fi

launcher="$repo/build/launcher/macos/thorium_launcher"
if [[ $use_launcher -eq 1 && ! -x "$launcher" ]]; then
    echo "No launcher built at $launcher -- running the server on its own." >&2
    echo "(Build it with: cmake --preset macos-launcher && cmake --build build/launcher)" >&2
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

echo "thorium_webui: $webui"
echo "run_scripts:   $run_scripts"
echo "console:       http://127.0.0.1:$port/"
echo "logs:          $repo/build/logs"

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
        if command -v open >/dev/null; then
            open "http://127.0.0.1:$port/"
        elif command -v xdg-open >/dev/null; then
            xdg-open "http://127.0.0.1:$port/" >/dev/null 2>&1
        fi
        break
    fi
    kill -0 "$server" 2>/dev/null || break
    sleep 0.1
done

wait "$server"
