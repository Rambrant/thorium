#!/usr/bin/env python3
#
# Stands in for thorium_webui in teardown.sh: binds 127.0.0.1:<port> and
# answers the one request the launcher makes of a server, GET /api/status,
# the way the real one does -- as a console with no page open and no run in
# flight. Nothing else about the console is imitated.
#
# With a third argument it also stands in for a run in flight: a child in the
# same process group that takes SIGTERM the way run_scripts does -- as a
# request to wind down, which takes it a few seconds -- and writes that file
# once it has. teardown.sh uses it to see that the launcher's watchdog lets a
# run finish safing rather than killing it part way.
#
import http.server
import subprocess
import sys

SLOW_TO_STOP = """
import signal, sys, time

def wind_down(*_):
    time.sleep(4)
    open(sys.argv[1], "w").write("wound down\\n")
    sys.exit(0)

signal.signal(signal.SIGTERM, wind_down)
while True:
    time.sleep(60)
"""

if len(sys.argv) > 2:
    subprocess.Popen([sys.executable, "-c", SLOW_TO_STOP, sys.argv[2]])


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'{"viewers":0,"running":false}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
