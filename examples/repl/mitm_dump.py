import json
import datetime
import hashlib
from email import message_from_bytes
from email.policy import default

from mitmproxy import http


def _dump_sdk_request(req: http.Request, content_type: str):
    print("")
    printed_json = False
    if "application/json" in content_type:
        try:
            parsed = json.loads(req.get_text(strict=False))
            print(json.dumps(parsed, indent=2))
            printed_json = True
        except Exception:
            pass
    
    if not printed_json:
        try:
            text = req.get_text(strict=False)
            print(text)
        except Exception as e:
            print(f"[Cannot decode body: {e}]")


def _dump_crashpad_handler_request(req: http.Request, content_type: str):
    try:
        # Get the raw content
        content = req.content

        # Parse as MIME message
        mime_headers = f"Content-Type: {content_type}\r\n\r\n"
        mime_message = mime_headers.encode() + content
        msg = message_from_bytes(mime_message, policy=default)

        if msg.is_multipart():
            print("Form data:")

            for part in msg.iter_parts():
                content_disposition = part.get("Content-Disposition", "")

                # Extract field name
                name = None
                filename = None
                if "name=" in content_disposition:
                    for item in content_disposition.split(";"):
                        item = item.strip()
                        if item.startswith('name="'):
                            name = item[6:-1]
                        elif item.startswith("filename="):
                            filename = item[10:-1]

                if name:
                    print(f"\nField: {name}")

                    if filename:
                        print(f"Filename: {filename}")
                        # Binary file - show size and md5
                        part_content = part.get_content()
                        if isinstance(part_content, str):
                            part_content = part_content.encode()

                        md5_hash = hashlib.md5(part_content).hexdigest()
                        print(f"Size: {len(part_content)} bytes")
                        print(f"MD5: {md5_hash}")
                    else:
                        # Text field - print as-is
                        value = part.get_content()
                        if isinstance(value, bytes):
                            value = value.decode('utf-8', errors='replace')
                        print(f"Value: {value}")
            print("")
        else:
            print(f"[Body of request with Content-Type {content_type} is not multipart]")
    except Exception as e:
        print(f"[Error parsing multipart data: {e}]")


def request(flow: http.HTTPFlow):
    # Print the basic details and headers from the intercepted request
    req = flow.request
    status_line = f"{req.method} {req.path} {req.http_version}"
    print("")
    print(f"============== {datetime.datetime.now().isoformat()} ==============")
    print(f"> {req.method} {req.pretty_url}")
    print("-" * 56)
    print(status_line)
    for k, v in req.headers.items():
        if any(s in k.lower() for s in ('api-key', 'secret')):
            v = '*' * len(v)
        print(f"{k}: {v}")

    print("")
    content_type = req.headers.get("content-type", "")

    # In testing, we may configure the Crashpad handler to upload via our proxy, so that
    # we can intercept requests and dump them. These requests use multipart/form-data,
    # and they are _not_ forwarded to Datadog intake if running in reverse-proxy mode.
    if "multipart/form-data" in content_type:
        _dump_crashpad_handler_request(req, content_type)
        flow.response = http.Response.make(204, b"")

    # For all other requests, dump the JSON payload and forward them to intake
    elif "application/json" in content_type:
        _dump_sdk_request(req, content_type)
        # flow.response remains unset; mitmproxy will forward the request


def response(flow: http.HTTPFlow):
    s = f"{flow.response.status_code} {flow.response.reason}"
    if len(s) >= 54:
        print(s)
    else:
        print(f" {s} ".center(56, '='))
