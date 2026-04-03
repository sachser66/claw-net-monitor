#!/usr/bin/env bash
set -euo pipefail

URL="${CLAW_MONITOR_SUMMARY_URL:-http://127.0.0.1:8080/api/summary}"
TIMEOUT="${CLAW_MONITOR_SUMMARY_TIMEOUT:-5}"

if ! command -v curl >/dev/null 2>&1; then
  echo "monitor-summary: curl fehlt" >&2
  exit 1
fi

body="$(curl -fsS --max-time "$TIMEOUT" "$URL" || true)"

if [ -z "$body" ]; then
  echo "Monitor summary nicht erreichbar (${URL})"
  exit 2
fi

printf '%s\n' "$body"
