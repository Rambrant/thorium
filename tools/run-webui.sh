#!/usr/bin/env bash
#
# Starts the bench console: thorium_webui in front of an installed run_scripts,
# either behind framework/launcher (menu-bar or tray icon, Chrome app window) or
# on its own with the page opened in the default browser.
#
# macOS and Windows both. On Windows it wants Git Bash, like every other script
# under tools/ (see README.md's "Tests"), rather than a .ps1 copy to keep in
# step with this one.
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

# --- What differs between the two hosts ---
# Everything platform-specific is here, so the rest of the script reads the
# same on both: the executable suffix, where the launcher is built, what its
# icon is called, and three tools Git Bash does not ship -- lsof, pgrep and
# open -- replaced by the Windows commands that answer the same questions.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) windows=1 ;;
    *)                    windows=0 ;;
esac

if [[ $windows -eq 1 ]]; then
    exe=".exe"
    launcher="$repo/build/launcher/thorium_launcher.exe"
    launcher_preset="windows-launcher"
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
    launcher="$repo/build/launcher/macos/thorium_launcher"
    launcher_preset="macos-launcher"
    icon="menu-bar icon"

    port_in_use() { lsof -nP -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1; }
    is_running()  { pgrep -f "$1 --" >/dev/null 2>&1; }
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

run_scripts="$repo/build/install/bin/run_scripts$exe"
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
        candidate="$repo/build/$tree/framework/webui/thorium_webui$exe"
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
webui="$(native_path "$(cd "$(dirname "$webui")" && pwd)/$(basename "$webui")")"
run_scripts="$(native_path "$(cd "$(dirname "$run_scripts")" && pwd)/$(basename "$run_scripts")")"

[[ -f "$(dirname "$run_scripts")/manifest.json" ]] ||
    echo "Note: no manifest.json beside $run_scripts -- the criteria picker will be a text field." >&2

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

if [[ $use_launcher -eq 1 && ! -x "$launcher" ]]; then
    echo "No launcher built at $launcher -- running the server on its own." >&2
    echo "(Build it from framework/launcher: cmake --preset $launcher_preset && cmake --build --preset $launcher_preset)" >&2
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
