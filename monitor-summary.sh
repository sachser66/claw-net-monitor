#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-short}"
BASE_URL="${CLAW_MONITOR_SUMMARY_BASE_URL:-http://127.0.0.1:8080}"
TIMEOUT="${CLAW_MONITOR_SUMMARY_TIMEOUT:-5}"

case "$MODE" in
  short|monitor|kurz)
    URL="$BASE_URL/api/summary"
    ;;
  full)
    URL="$BASE_URL/api/summary-full"
    ;;
  *)
    echo "Usage: $0 [short|full]" >&2
    exit 64
    ;;
esac

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
