# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for running an in-process HTTP server to handle requests from the SDK.
"""

import http.server
import threading
from typing import Optional


class SimpleHTTPRequestHandler(http.server.BaseHTTPRequestHandler):
    """HTTP request handler that responds with 202 Accepted to all requests."""

    def do_POST(self):
        self.send_response(202)
        self.end_headers()

    def log_message(self, format, *args):
        """Suppress default logging."""
        pass


class HTTPServer:
    """HTTP server that can be started and stopped cleanly from a background thread."""

    def __init__(self, host: str = "127.0.0.1", port: int = 14404):
        self.host = host
        self.port = port
        self.server: Optional[http.server.HTTPServer] = None
        self.thread: Optional[threading.Thread] = None

    def start(self):
        """Starts the HTTP server on a background thread."""
        if self.server is not None:
            raise RuntimeError("Server is already running")

        self.server = http.server.HTTPServer(
            (self.host, self.port), SimpleHTTPRequestHandler
        )
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def stop(self):
        """Stops the HTTP server and waits for the thread to finish."""
        if self.server is None:
            return

        self.server.shutdown()
        self.server.server_close()
        if self.thread is not None:
            self.thread.join()
        self.server = None
        self.thread = None
