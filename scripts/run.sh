#!/usr/bin/env bash
set -euo pipefail

# Quick helper to configure, build, and run the sample.
# Usage:
#   ./scripts/run.sh             # Debug build in ./build
#   BUILD_DIR=out BUILD_TYPE=Release ./scripts/run.sh

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

cmake -S "$ROOT_DIR" -B "$ROOT_DIR/$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$ROOT_DIR/$BUILD_DIR" --parallel

BIN_NAME="super_mario_proto"
case "${OSTYPE:-}" in
  msys*|cygwin*|win32*) BIN_NAME="${BIN_NAME}.exe" ;;
esac

BIN_PATH="$ROOT_DIR/$BUILD_DIR/$BIN_NAME"
if [[ ! -x "$BIN_PATH" ]]; then
  echo "Built binary not found at $BIN_PATH" >&2
  exit 1
fi

# Prefer explicit ASSETS_ROOT; fall back to project assets.
export ASSETS_ROOT="${ASSETS_ROOT:-$ROOT_DIR/assets}"

exec "$BIN_PATH"


