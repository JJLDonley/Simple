#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
EXT_DIR="$ROOT/Editor/vscode-simple"

if [[ ! -d "$EXT_DIR" ]]; then
  echo "error: extension directory not found: $EXT_DIR" >&2
  exit 1
fi

SKIP_NPM=0
if [[ "${1:-}" == "--skip-npm" ]]; then
  SKIP_NPM=1
fi

if ! command -v npx >/dev/null 2>&1; then
  echo "error: npx not found (install Node.js + npm)" >&2
  exit 1
fi

pushd "$EXT_DIR" >/dev/null

if [[ "$SKIP_NPM" -eq 0 ]]; then
  if [[ ! -d node_modules ]]; then
    if ! command -v npm >/dev/null 2>&1; then
      echo "error: npm not found (install Node.js + npm) or pass --skip-npm" >&2
      popd >/dev/null
      exit 1
    fi
    npm install
  fi
fi

VSIX_PATH="$EXT_DIR/simple-vscode.vsix"
npx --yes @vscode/vsce package --out "$VSIX_PATH"

if command -v code >/dev/null 2>&1; then
  code --install-extension "$VSIX_PATH" --force
  echo "installed: $VSIX_PATH"
  popd >/dev/null
  exit 0
fi

echo "warning: VS Code 'code' CLI not found; VSIX built at $VSIX_PATH" >&2
popd >/dev/null
exit 2
