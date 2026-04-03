#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CPP_DIR="$ROOT/cpp"
BUILD_DIR="$CPP_DIR/build"
BIN="$BUILD_DIR/claw-net-monitor"
PORT="${CLAW_MONITOR_PORT:-8080}"
LOCK_DIR="${XDG_RUNTIME_DIR:-/tmp}/claw-net-monitor.lock"
PID_FILE="$LOCK_DIR/pid"
MODE="${1:-tui}"

mkdir -p "$BUILD_DIR"
mkdir -p "$LOCK_DIR"

cleanup() {
  if [ -f "$PID_FILE" ] && [ "$(cat "$PID_FILE" 2>/dev/null || true)" = "$$" ]; then
    rm -f "$PID_FILE"
    rmdir "$LOCK_DIR" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if [ -f "$PID_FILE" ]; then
  OLD_PID="$(cat "$PID_FILE" 2>/dev/null || true)"
  if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
    echo "[claw-net-monitor] läuft bereits mit PID $OLD_PID"
    echo "[claw-net-monitor] stoppe alten Prozess oder beende ihn sauber, bevor du neu startest"
    exit 1
  fi
  rm -f "$PID_FILE"
fi
printf '%s
' "$$" > "$PID_FILE"

needs_build=0
if [ ! -x "$BIN" ]; then
  needs_build=1
elif find "$CPP_DIR" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name 'CMakeLists.txt' \) -newer "$BIN" -print -quit | grep -q .; then
  needs_build=1
fi

if [ "$needs_build" = "1" ]; then
  echo "[claw-net-monitor] baue aktuellen Stand..."
  cmake -S "$CPP_DIR" -B "$BUILD_DIR"
  cmake --build "$BUILD_DIR" -j
fi

HOST_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
HOST_IP="${HOST_IP:-127.0.0.1}"
echo "[claw-net-monitor] iPhone/Browser: http://${HOST_IP}:${PORT}"

if [ "$MODE" = "headless" ] || [ "${CLAW_MONITOR_HEADLESS:-}" = "1" ]; then
  echo "[claw-net-monitor] starte Headless + Mobile-View"
  exec env CLAW_MONITOR_HEADLESS=1 "$BIN"
fi

if [ "$MODE" != "tui" ]; then
  echo "[claw-net-monitor] unbekannter Modus: $MODE (erlaubt: tui | headless)"
  exit 1
fi

echo "[claw-net-monitor] starte TUI + Mobile-View"
exec "$BIN"
