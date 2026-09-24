#!/bin/sh
#
# The launcher's promise, end to end, against the real binary: a second
# launcher refuses to start, and when the launcher dies -- here by kill -9,
# the case no destructor sees -- nothing it started is left running. That is
# process_group.hpp's watchdog doing its job, and the one thing about this
# program that matters most if it goes wrong: a leaked run_scripts is a rig
# nobody is watching.
#
# Usage: teardown.sh <path to thorium_launcher>
#
# Needs a logged-in desktop and Chrome, since it starts the real thing:
# skipped (exit 77) without either. It opens one Chrome app-mode window for
# a few seconds, against a throwaway $HOME so it cannot collide with a
# console that is really running. The "server" is Python's http.server --
# the launcher only needs something that binds the port, and this test is
# about the process group, not about thorium_webui.
#
# A clean Quit is not driven from here: it needs a click on the menu bar,
# which needs Accessibility permission a test cannot grant itself.
# test_menu.cpp covers the Quit handler and stop(); what Quit then does to
# the children -- close the watchdog's pipe -- is the same close a kill -9
# causes, tested below.
#
set -u

LAUNCHER=$1
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
    rm -rf "$TEST_HOME"
}

members() { ps -A -o pgid= | awk -v g="$1" '$1 == g' | wc -l | tr -d ' '; }
listening() { curl -s -m 1 -o /dev/null "http://127.0.0.1:$PORT/"; }

listening && skip "port $PORT is already in use"

# --- Start it --------------------------------------------------------------
HOME=$TEST_HOME "$LAUNCHER" \
    --server="$PYTHON" \
    --server-arg=-m --server-arg=http.server \
    --server-arg=--bind --server-arg=127.0.0.1 \
    --server-arg="$PORT" \
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

# --- A second launcher refuses ----------------------------------------------
# It reports on stderr, then waits on an alert nobody will dismiss -- so read
# the message, check it started nothing, and kill it.
HOME=$TEST_HOME "$LAUNCHER" --server=/usr/bin/false >"$TEST_HOME/second.log" 2>&1 &
SECOND_PID=$!
for _ in $(seq 1 25); do grep -q "already running" "$TEST_HOME/second.log" && break; sleep 0.2; done
grep -q "already running" "$TEST_HOME/second.log" \
    || { kill -9 "$SECOND_PID" 2>/dev/null; fail "a second launcher did not refuse: $(cat "$TEST_HOME/second.log")"; }
[ -z "$(pgrep -P "$SECOND_PID")" ] \
    || { kill -9 "$SECOND_PID" 2>/dev/null; fail "the refused launcher started children anyway"; }
kill -9 "$SECOND_PID" 2>/dev/null
echo "second instance refused"

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
