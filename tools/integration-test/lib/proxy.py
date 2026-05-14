# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Proxy implementation that uses mitmproxy to intercept HTTP requests, forward them to a
remote host, and report them to the main thread.
"""
import re
import json
import asyncio
import threading
from urllib.parse import urlparse
from collections import defaultdict
from dataclasses import dataclass
from typing import Optional, List, Dict

from mitmproxy.tools.dump import DumpMaster
from mitmproxy import http, options


# We configure each SDK instance with a custom endpoint URL that will direct requests to
# our mitmproxy server, which will intercept and buffer them. In order to handle
# requests from multiple SDK instances concurrently, we may configure that URL to
# identify the client, e.g. 'http://localhost:8080/sdk-2/api/v2/logs': if the first part
# of the path matches this pattern, we will parse the ID and strip the prefix from the
# path before forwarding the request.
__sdk_id_path_token_pattern__ = re.compile(r'^sdk-(\d+)$')


@dataclass
class CapturedUrl:
    """Wrapper for 6-element named tuple returned by urlparse."""
    scheme: str
    netloc: str
    path: str
    params: str
    query: str
    fragment: str


@dataclass
class CapturedRequest:
    sdk_id: Optional[int]
    method: str
    url: CapturedUrl
    headers: dict[str, str]
    body: bytes

    @property
    def json(self):
        return json.loads(self.body)


class ProxyAddon:
    def __init__(self):
        self._lock = threading.Lock()
        self._requests: Dict[Optional[int], List[CapturedRequest]] = defaultdict(list)

    def request(self, flow: http.HTTPFlow) -> None:
        # Examine the first part of the request URL's path
        path = flow.request.path
        path_parts = path.lstrip('/').split('/', 1)

        # Check to see if the path begins with '/sdk-%d'
        sdk_id: Optional[int] = None
        sdk_id_match = __sdk_id_path_token_pattern__.match(path_parts[0])
        if sdk_id_match:
            # If so, parse the ID and stash it in the request's metadata
            sdk_id = int(sdk_id_match.group(1))
            flow.metadata['sdk_id'] = sdk_id

            # Rewrite the path to remove the '/sdk-%d' prefix before forwarding
            if len(path_parts) > 1:
                _, tail = path_parts
                flow.request.path = '/' + tail
            else:
                flow.request.path = '/'

        captured = CapturedRequest(
            sdk_id=sdk_id,
            method=flow.request.method,
            url=urlparse(flow.request.url),
            headers=dict(flow.request.headers),
            body=flow.request.content,
        )
        with self._lock:
            self._requests[sdk_id].append(captured)

        # Return a synthetic 200 OK directly rather than forwarding to the upstream
        # intake endpoint. This avoids needing real credentials and eliminates network
        # round-trips to Datadog servers.
        flow.response = http.Response.make(200, b'{}', {'Content-Type': 'application/json'})

    def drain(self, sdk_id: int) -> List[CapturedRequest]:
        """
        Returns and removes all captured requests for the given `sdk_id`. Calling this
        after the corresponding process has exited is safe: all of that process's HTTP
        requests are guaranteed to have been captured before the process exits, since the
        SDK waits for HTTP responses before completing a flush, and this hook fires before
        any response is sent.
        """
        with self._lock:
            return self._requests.pop(sdk_id, [])

        # We're spoofing the intake endpoint: just return 200
        flow.response = http.Response.make(200, b'{}', {'Content-Type': 'application/json'})


class ProxyServer:
    def __init__(
        self,
        port: int,
        host: str = "0.0.0.0"
    ):
        self.port = port
        self.host = host

        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._master: Optional[DumpMaster] = None
        self._thread: Optional[threading.Thread] = None
        self._addon = ProxyAddon()

    def start(self) -> None:
        """
        Starts mitmproxy in a background thread with its own asyncio event loop.
        """
        def _run() -> None:
            # Configure mitmproxy to listen on the configured port
            opts = options.Options(
                listen_host=self.host,
                listen_port=self.port
            )

            # Initialize a mitmdump entry point to be run in-process, on a dedicated
            # asyncio event loop
            self._loop = asyncio.new_event_loop()
            self._master = DumpMaster(
                opts,
                self._loop,
                with_termlog=False,
                with_dumper=False,
            )

            # Register our mitmproxy Addon so we can intercept requests as they're
            # forwarded
            self._master.addons.add(self._addon)

            # Run until shutdown is triggered
            self._loop.run_until_complete(self._master.run())

        # Run our dedicated mitmproxy server on a background thread
        self._thread = threading.Thread(target=_run, daemon=True)
        self._thread.start()

    def drain(self, sdk_id: int) -> List[CapturedRequest]:
        """Delegates to the addon's drain method."""
        return self._addon.drain(sdk_id)

    def stop(self) -> None:
        """
        Cleanly stops mitmproxy and joins on the background thread.
        """
        if not self._loop or not self._master:
            return

        async def _shutdown() -> None:
            await self._master.shutdown()

        # Schedule shutdown on the proxy event loop
        asyncio.run_coroutine_threadsafe(_shutdown(), self._loop)

        # Wait for the proxy thread to finish
        if self._thread is not None:
            self._thread.join()
