#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPL_ENV="$REPO_ROOT/.repl-env"

if [[ ! -f "$REPL_ENV" ]]; then
  echo "error: .repl-env not found at $REPL_ENV" >&2
  exit 1
fi

client_token=$(grep 'set-config client-token' "$REPL_ENV" | awk '{print $3}')
if [[ -z "$client_token" ]]; then
  echo "error: client-token not found in .repl-env" >&2
  exit 1
fi

json_payload=$(python3 "$REPO_ROOT/tools/process-crash-report/main.py" --tool none --output json)
if [[ -z "$json_payload" ]]; then
  echo "error: process-crash-report produced no output" >&2
  exit 1
fi

request_id=$(python3 -c 'import uuid; print(uuid.uuid4())')

curl -sf \
  -X POST "https://browser-intake-datadoghq.com/api/v2/rum?ddsource=unity" \
  -H "Content-Type: application/json" \
  -H "DD-API-KEY: $client_token" \
  -H "DD-EVP-ORIGIN: unity" \
  -H "DD-EVP-ORIGIN-VERSION: 0.2.0" \
  -H "DD-REQUEST-ID: $request_id" \
  -H "User-Agent: nobody" \
  -d "[$json_payload]"
