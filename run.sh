#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CPP_DIR="$ROOT/cpp"
BUILD_DIR="$CPP_DIR/build"
BIN="$BUILD_DIR/claw-net-monitor"
PORT="${CLAW_MONITOR_PORT:-8080}"

mkdir -p "$BUILD_DIR"

if [ ! -x "$BIN" ]; then
  echo "[claw-net-monitor] build fehlt -> kompiliere..."
  cmake -S "$CPP_DIR" -B "$BUILD_DIR"
  cmake --build "$BUILD_DIR" -j
fi

echo "[claw-net-monitor] starte TUI + Mobile-View"
echo "[claw-net-monitor] iPhone/Browser: http://$(hostname -I | awk '{print $1}'):${PORT}"
exec "$BIN"
