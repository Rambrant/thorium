#!/bin/sh
#
# The launcher's promise, end to end, against the real binary: a second
# launcher refuses to start, and when the launcher dies -- here by kill -9,
# the case no destructor sees -- nothing it started is left running. That is
# process_group.hpp's watchdog doing its job, and the one thing about this
# program that matters most if it goes wrong: a leaked run_scripts is a rig
# nobody is watching.
#
# Usage: teardown.sh <path to thorium_launcher> <path to stand_in_server.py>
#
# Needs a logged-in desktop and Chrome, since it starts the real thing:
# skipped (exit 77) without either. It opens one Chrome app-mode window for
# a few seconds, against a throwaway $HOME so it cannot collide with a
# console that is really running. The "server" is stand_in_server.py -- the
# launcher needs something that answers /api/status, and this test is about
# the process group, not about thorium_webui.
#
# A clean exit (the operator closing the window) is not driven from here: it
# needs a real console page holding /api/presence open. What it does to the
# children -- close the watchdog's pipe -- is the same close a kill -9 causes,
# tested below.
#
set -u

LAUNCHER=$1
STAND_IN=$2
PORT=8499
PYTHON=/usr/bin/python3
CHROME="/Applications/Google Chrome.app"

skip() { echo "skipped: $1"; exit 77; }
fail() { echo "FAIL: $1"; cleanup; exit 1; }

launchctl managername 2>/dev/null | grep -q Aqua || skip "no logged-in desktop session"
[ -x "$PYTHON" ] || skip "no $PYTHON to stand in for the server"
mdfind "kMDItemCFBundleIdentifier == 'com.google.Chrome'" | grep -q . || [ -d "$CHROME" ] || skip "Chrome is not installed"

TEST_HOME=$(mktemp -d "${TMPDIR:-/tmp}/thorium-launcher-test.XXXXXX")
LAUNCHER_PID=""
GROUP=""

cleanup()
{
    [ -n "$LAUNCHER_PID" ] && kill -9 "$LAUNCHER_PID" 2>/dev/null
    [ -n "$GROUP" ] && kill -9 -"$GROUP" 2>/dev/null
    # Anything still on the throwaway profile, whatever group it is in -- the
    # second launcher's Chrome is deliberately outside it.
    pkill -9 -f "user-data-dir=$TEST_HOME" 2>/dev/null
    rm -rf "$TEST_HOME"
}

members() { ps -A -o pgid= | awk -v g="$1" '$1 == g' | wc -l | tr -d ' '; }
listening() { curl -s -m 1 -o /dev/null "http://127.0.0.1:$PORT/"; }

listening && skip "port $PORT is already in use"

# --- Start it --------------------------------------------------------------
# --use-mock-keychain, because the throwaway $HOME has no
# Library/Keychains: a Chrome started there asks for one to keep its
# "Chrome Safe Storage" key in, and macOS puts up "keychain not found" --
# a dialog that sits on the developer's desktop until the kill below takes
# it away, on every test run.
HOME=$TEST_HOME "$LAUNCHER" \
    --server="$PYTHON" \
    --browser-arg=--use-mock-keychain \
    --server-arg="$STAND_IN" --server-arg="$PORT" \
    --port="$PORT" >"$TEST_HOME/launcher.log" 2>&1 &
LAUNCHER_PID=$!

for _ in $(seq 1 50); do listening && break; sleep 0.2; done
listening || fail "the stand-in server never came up: $(cat "$TEST_HOME/launcher.log")"

SERVER_PID=$(lsof -t -iTCP:"$PORT" -sTCP:LISTEN | head -1)
GROUP=$(ps -o pgid= -p "$SERVER_PID" | tr -d ' ')
[ -n "$GROUP" ] || fail "could not find the server's process group"
[ "$(ps -o pgid= -p "$LAUNCHER_PID" | tr -d ' ')" != "$GROUP" ] \
    || fail "the server is in the launcher's own group, not the watchdog's"

# Chrome joins the same group once it is up.
for _ in $(seq 1 50); do
    ps -A -o pgid=,comm= | awk -v g="$GROUP" '$1 == g' | grep -q "Google Chrome" && break
    sleep 0.2
done
ps -A -o pgid=,comm= | awk -v g="$GROUP" '$1 == g' | grep -q "Google Chrome" \
    || fail "Chrome did not start in the console's process group"
echo "started: launcher $LAUNCHER_PID, group $GROUP with $(members "$GROUP") processes"

# --- A second launcher asks for a window instead -----------------------------
# With no menu to ask the first one from, running the launcher again is how an
# operator whose window is closed gets it back. It must not start a second
# console: it hands the window to the Chrome already on the profile, and exits.
SERVER_BEFORE=$(lsof -t -iTCP:"$PORT" -sTCP:LISTEN | head -1)
HOME=$TEST_HOME "$LAUNCHER" --server=/usr/bin/false --port="$PORT" --browser-arg=--use-mock-keychain \
    >"$TEST_HOME/second.log" 2>&1 &
SECOND_PID=$!
for _ in $(seq 1 50); do kill -0 "$SECOND_PID" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$SECOND_PID" 2>/dev/null; then
    kill -9 "$SECOND_PID" 2>/dev/null
    fail "a second launcher did not exit: $(cat "$TEST_HOME/second.log")"
fi
wait "$SECOND_PID" || fail "a second launcher failed: $(cat "$TEST_HOME/second.log")"
[ "$(lsof -t -iTCP:"$PORT" -sTCP:LISTEN | head -1)" = "$SERVER_BEFORE" ] \
    || fail "the second launcher disturbed the console server"

# Its Chrome is outside the group and has to hand the window to the running
# one and exit. Wait for that: killing the first Chrome before it has would
# leave this one as the only browser on the profile -- a stray window, open on
# a console that is about to be gone.
for _ in $(seq 1 100); do
    pgrep -f "user-data-dir=$TEST_HOME" | while read -r pid; do
        [ "$(ps -o pgid= -p "$pid" | tr -d ' ')" = "$GROUP" ] || echo "$pid"
    done | grep -q . || break
    sleep 0.1
done
OUTSIDERS=$(pgrep -f "user-data-dir=$TEST_HOME" | while read -r pid; do
    [ "$(ps -o pgid= -p "$pid" | tr -d ' ')" = "$GROUP" ] || echo "$pid"
done)
[ -z "$OUTSIDERS" ] || fail "the second launcher's Chrome did not hand its window over: $OUTSIDERS"
echo "second instance opened a window and exited"

# --- kill -9 the launcher ---------------------------------------------------
kill -9 "$LAUNCHER_PID"
LAUNCHER_PID=""

# Two seconds of SIGTERM grace, then SIGKILL -- allow a little over that.
for _ in $(seq 1 40); do [ "$(members "$GROUP")" = 0 ] && break; sleep 0.1; done

LEFT=$(members "$GROUP")
[ "$LEFT" = 0 ] || fail "$LEFT processes of the console survived the launcher: $(ps -A -o pid=,pgid=,comm= | awk -v g="$GROUP" '$2 == g')"
listening && fail "the server is still answering on port $PORT"
GROUP=""
echo "kill -9 of the launcher took everything it started with it"

cleanup
exit 0
