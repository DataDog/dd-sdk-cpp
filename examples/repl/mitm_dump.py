import json
import datetime

from mitmproxy import http


def request(flow: http.HTTPFlow):
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
    printed_json = False
    if "application/json" in req.headers.get("content-type", ""):
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


def response(flow: http.HTTPFlow):
    s = f"{flow.response.status_code} {flow.response.reason}"
    if len(s) >= 54:
        print(s)
    else:
        print(f" {s} ".center(56, '='))
