#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$ROOT_DIR/bin"
DIST_DIR="$ROOT_DIR/dist"

VERSION="dev"
TARGET=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="${2:-}"
      shift 2
      ;;
    --target)
      TARGET="${2:-}"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

if [[ -z "$TARGET" ]]; then
  os="$(uname -s | tr '[:upper:]' '[:lower:]')"
  arch="$(uname -m)"
  TARGET="${os}-${arch}"
fi

if [[ ! -x "$BIN_DIR/simple" || ! -f "$BIN_DIR/libsimplevm_runtime.a" || ! -f "$BIN_DIR/libsimplevm_runtime.so" ]]; then
  echo "missing built artifacts in $BIN_DIR (run ./Simple/build.sh first)" >&2
  exit 1
fi

PKG_NAME="simple-${VERSION}-${TARGET}"
STAGE_DIR="$DIST_DIR/$PKG_NAME"
PKG_PATH="$DIST_DIR/${PKG_NAME}.tar.gz"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/bin" "$STAGE_DIR/lib" "$STAGE_DIR/include/simplevm" "$STAGE_DIR/share/simple"

cp "$BIN_DIR/simple" "$STAGE_DIR/bin/simple"
cp "$BIN_DIR/simplevm" "$STAGE_DIR/bin/simplevm"
cp "$BIN_DIR/libsimplevm_runtime.a" "$STAGE_DIR/lib/libsimplevm_runtime.a"
cp "$BIN_DIR/libsimplevm_runtime.so" "$STAGE_DIR/lib/libsimplevm_runtime.so"

cp "$ROOT_DIR/VM/include/"*.h "$STAGE_DIR/include/simplevm/"
cp "$ROOT_DIR/Byte/include/"*.h "$STAGE_DIR/include/simplevm/"
cp "$ROOT_DIR/Docs/"*.md "$STAGE_DIR/share/simple/"

mkdir -p "$DIST_DIR"
tar -czf "$PKG_PATH" -C "$DIST_DIR" "$PKG_NAME"

echo "built release: $PKG_PATH"
