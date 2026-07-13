#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
EXT_DIR="$ROOT_DIR/Editor/vscode-simple"
OUT_DIR="$ROOT_DIR/dist"
VSIX_OUT="$OUT_DIR/simple-vscode.vsix"
INSTALL=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--install]

Builds the Simple LSP executable and packages the VS Code extension as:
  $VSIX_OUT

Options:
  --install  Install the generated VSIX with the 'code' command after packaging.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install) INSTALL=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

mkdir -p "$BUILD_DIR" "$OUT_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target simplevm -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"

cd "$EXT_DIR"
if [[ -f package-lock.json ]]; then
  npm ci
else
  npm install
fi
npx --yes @vscode/vsce package --out "$VSIX_OUT"

cat <<DONE

Built VS Code package:
  $VSIX_OUT

LSP executable built at:
  $BUILD_DIR/bin/simplevm

Recommended VS Code settings if simple/simplevm is not on PATH:
{
  "simple.lspPath": "$BUILD_DIR/bin/simplevm",
  "simple.lspArgs": ["lsp"]
}
DONE

if [[ "$INSTALL" -eq 1 ]]; then
  code --install-extension "$VSIX_OUT" --force
fi
