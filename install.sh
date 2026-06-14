#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"

"$ROOT_DIR/build.sh"

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

path_has_dir() {
  case ":$PATH:" in
    *":$1:"*) return 0 ;;
    *) return 1 ;;
  esac
}

profile_file=""
if [[ -n "${SIMPLE_PROFILE:-}" ]]; then
  profile_file="$SIMPLE_PROFILE"
elif [[ "${SHELL:-}" == */zsh ]]; then
  profile_file="$HOME/.zshrc"
elif [[ "${SHELL:-}" == */bash ]]; then
  profile_file="$HOME/.bashrc"
else
  profile_file="$HOME/.profile"
fi

path_status="already on PATH"
if ! path_has_dir "$BINDIR"; then
  mkdir -p "$(dirname "$profile_file")"
  touch "$profile_file"
  if ! grep -F "export PATH=\"$BINDIR:\$PATH\"" "$profile_file" >/dev/null 2>&1; then
    {
      echo ""
      echo "# Simple language PATH"
      echo "export PATH=\"$BINDIR:\$PATH\""
    } >> "$profile_file"
    path_status="added to $profile_file; restart your shell or run: export PATH=\"$BINDIR:\$PATH\""
  else
    path_status="configured in $profile_file; restart your shell"
  fi
fi

cat <<EOF
Installed:
  $BINDIR/svm
  $BINDIR/simple

PATH: $path_status
EOF
