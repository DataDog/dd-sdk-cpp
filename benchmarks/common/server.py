import time
import random
import argparse
import socketserver
import socket

__enable_logging__ = True


class DelayedHandler(socketserver.BaseRequestHandler):
    def __init__(self, request, client_address, server, response_delay_ms):
        # Store the desired delay between opening the connection and sending a response 
        self.response_delay = (response_delay_ms / 1000.0)
        super().__init__(request, client_address, server)

    def handle(self):
        # Keep track of timing so we can deduct socket read time from our delay
        started_recv_at = time.time()

        # Consume the full request
        sock = self.request # type: socket.socket
        sock.settimeout(0.05)
        data = b''
        num_bytes_recvd = 0
        request_line = ''
        while True:
            try:
                chunk = sock.recv(8192)
                if not chunk:
                    break

                # Accumulate the size of the request (not the body; the whole request)
                num_bytes_recvd += len(chunk)

                # Read into a buffer until we've grabbed the first CRLF-delimited line
                if not request_line:
                    data += chunk
                    crlf_pos = data.index(b'\r\n')
                    if crlf_pos >= 0:
                        request_line = data[0:crlf_pos].decode()
            except socket.timeout:
                break
            except (ConnectionResetError, BrokenPipeError):
                self._log('Connection closed by client')
                sock.close()
                return

        # Validate the request, mostly just so we can log the path
        tokens = request_line.split(' ')
        if len(tokens) != 3:
            self._log('Invalid request')
            sock.close()
            return
        _, path, http_version = tokens
        if not path.startswith('/') or not http_version.startswith('HTTP/'):
            self._log('Invalid request')
            sock.close()
            return
        request_summary = '<n:%d> %s' % (num_bytes_recvd, request_line)

        # Figure out how long it took to read the request, and how much that cut into
        # our simulated response day
        finished_recv_at = time.time()
        recv_duration = finished_recv_at - started_recv_at
        remaining_response_delay = self.response_delay - recv_duration

        # Wait before responding, consistently, according to our configured delay
        if remaining_response_delay > 0.0:
            time.sleep(remaining_response_delay)
        
        # Send a hardcoded HTTP response
        response = (
            b"HTTP/1.1 202 Accepted\r\n"
            b"Content-Type: application/json\r\n"
            b"Content-Length: 2\r\n"
            b"Connection: close\r\n"
            b"\r\n"
            b"{}"
        )
        try:
            sock.sendall(response)
            self._log('202 %s' % request_summary)
        except (OSError, socket.timeout):
            self._log('RESPONSE NOT SENT %s' % request_summary)
        finally:
            sock.close()

    def _log(self, message):
        if __enable_logging__:
            remote_addr = '%s:%d' % self.client_address
            print('[%s] %s' % (remote_addr, message))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Mock HTTP server used for SDK benchmarks')
    parser.add_argument('--port', type=int, default=15101, help='Listen port to bind to on localhost')
    parser.add_argument('--response-delay-ms', type=int, default=20, help='Base delay, in milliseconds, to wait before sending a response')
    parser.add_argument('--response-delay-variability-ms', type=int, default=0, help='Randomized +/- delay applied to each response')
    args = parser.parse_args()

    lo = args.response_delay_ms - args.response_delay_variability_ms
    hi = args.response_delay_ms + args.response_delay_variability_ms
    if lo <= hi:
        min_response_delay_ms, max_response_delay_ms = max(0, lo), hi
    else:
        min_response_delay_ms, max_response_delay_ms = max(0, hi), lo

    def make_handler(request, client_address, server):
        response_delay = random.randint(min_response_delay_ms, max_response_delay_ms)
        return DelayedHandler(request, client_address, server, response_delay)

    with Server(('127.0.0.1', args.port), make_handler) as httpd:
        httpd.serve_forever()
