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

updated_active=()
install_active_command_if_safe() {
  local name="$1"
  local src="$2"
  local active=""
  active="$(command -v "$name" 2>/dev/null || true)"
  if [[ -z "$active" ]]; then
    return 0
  fi
  local active_real="$active"
  if command -v readlink >/dev/null 2>&1; then
    active_real="$(readlink -f "$active" 2>/dev/null || printf '%s' "$active")"
  fi
  if [[ "$active_real" == "$BINDIR/$name" ]]; then
    return 0
  fi
  case "$active_real" in
    "$HOME"/*)
      mkdir -p "$(dirname "$active_real")"
      install -m 0755 "$src" "$active_real"
      updated_active+=("$active -> $active_real")
      ;;
  esac
}

install_active_command_if_safe "svm" "$ROOT_DIR/bin/svm"
install_active_command_if_safe "simple" "$ROOT_DIR/bin/simple"

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

ensure_path_prepend() {
  mkdir -p "$(dirname "$profile_file")"
  touch "$profile_file"
  if ! grep -F "export PATH=\"$BINDIR:\$PATH\"" "$profile_file" >/dev/null 2>&1; then
    {
      echo ""
      echo "# Simple language PATH"
      echo "export PATH=\"$BINDIR:\$PATH\""
    } >> "$profile_file"
    return 0
  fi
  return 1
}

path_status="already on PATH"
if ! path_has_dir "$BINDIR"; then
  if ensure_path_prepend; then
    path_status="added to $profile_file; restart your shell or run: export PATH=\"$BINDIR:\$PATH\""
  else
    path_status="configured in $profile_file; restart your shell"
  fi
else
  resolved_svm="$(command -v svm 2>/dev/null || true)"
  resolved_svm_real="$resolved_svm"
  if [[ -n "$resolved_svm" ]] && command -v readlink >/dev/null 2>&1; then
    resolved_svm_real="$(readlink -f "$resolved_svm" 2>/dev/null || printf '%s' "$resolved_svm")"
  fi
  if [[ -n "$resolved_svm_real" && "$resolved_svm_real" != "$BINDIR/svm" ]]; then
    if ensure_path_prepend; then
      path_status="shadowed by $resolved_svm; prepended $BINDIR in $profile_file. Restart your shell or run: export PATH=\"$BINDIR:\$PATH\""
    else
      path_status="shadowed by $resolved_svm; $profile_file already prepends $BINDIR. Restart your shell or run: export PATH=\"$BINDIR:\$PATH\""
    fi
  fi
fi

cat <<EOF
Installed:
  $BINDIR/svm
  $BINDIR/simple

PATH: $path_status
EOF

if [[ ${#updated_active[@]} -gt 0 ]]; then
  echo "Updated active PATH command(s):"
  for item in "${updated_active[@]}"; do
    echo "  $item"
  done
fi
