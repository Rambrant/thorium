#!/usr/bin/env python3
#
# Stands in for thorium_webui in teardown.sh: binds 127.0.0.1:<port> and
# answers the one request the launcher makes of a server, GET /api/status,
# the way the real one does -- as a console with no page open and no run in
# flight. Nothing else about the console is imitated.
#
import http.server
import sys


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
