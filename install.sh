#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"

if [[ ! -x "$ROOT_DIR/bin/svm" || ! -x "$ROOT_DIR/bin/simple" ]]; then
  "$ROOT_DIR/build.sh"
fi

if [[ ! -d "$BINDIR" ]]; then
  mkdir -p "$BINDIR" 2>/dev/null || sudo mkdir -p "$BINDIR"
fi

install_bin() {
  local src="$1"
  local dst="$2"
  if [[ -w "$BINDIR" ]]; then
    install -m 0755 "$src" "$dst"
  else
    sudo install -m 0755 "$src" "$dst"
  fi
}

install_bin "$ROOT_DIR/bin/svm" "$BINDIR/svm"
install_bin "$ROOT_DIR/bin/simple" "$BINDIR/simple"

cat <<EOF
Installed:
  $BINDIR/svm
  $BINDIR/simple

Make sure $BINDIR is on PATH.
EOF
