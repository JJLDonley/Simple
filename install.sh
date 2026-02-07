#!/usr/bin/env bash
set -euo pipefail

PREFIX="${HOME}/.simple"
VERSION="latest"
BIN_DIR="${HOME}/.local/bin"
URL="${SIMPLE_RELEASE_URL:-}"
ARCHIVE_PATH=""
NO_LINK=0

usage() {
  cat <<'EOF'
Usage:
  ./install.sh [--url <release.tar.gz>] [--from-file <release.tar.gz>] [--prefix <dir>] [--version <name>] [--bin-dir <dir>] [--no-link]

Examples:
  ./install.sh --from-file ./dist/simple-dev-linux-x86_64.tar.gz --version dev
  ./install.sh --url https://example.com/simple-v0.1.0-linux-x86_64.tar.gz --version v0.1.0
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      PREFIX="${2:-}"
      shift 2
      ;;
    --version)
      VERSION="${2:-}"
      shift 2
      ;;
    --bin-dir)
      BIN_DIR="${2:-}"
      shift 2
      ;;
    --url)
      URL="${2:-}"
      shift 2
      ;;
    --from-file)
      ARCHIVE_PATH="${2:-}"
      shift 2
      ;;
    --no-link)
      NO_LINK=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$ARCHIVE_PATH" && -z "$URL" ]]; then
  echo "missing release source: pass --from-file or --url (or set SIMPLE_RELEASE_URL)" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ -n "$ARCHIVE_PATH" ]]; then
  if [[ ! -f "$ARCHIVE_PATH" ]]; then
    echo "archive not found: $ARCHIVE_PATH" >&2
    exit 1
  fi
  archive="$ARCHIVE_PATH"
else
  archive="$tmp_dir/simple-release.tar.gz"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$URL" -o "$archive"
  elif command -v wget >/dev/null 2>&1; then
    wget -qO "$archive" "$URL"
  else
    echo "need curl or wget to download release archive" >&2
    exit 1
  fi
fi

extract_dir="$tmp_dir/extract"
mkdir -p "$extract_dir"
tar -xzf "$archive" -C "$extract_dir"

release_root=""
for d in "$extract_dir"/*; do
  if [[ -d "$d" ]]; then
    release_root="$d"
    break
  fi
done
if [[ -z "$release_root" ]]; then
  echo "invalid archive layout: expected a top-level release directory" >&2
  exit 1
fi

for req in "$release_root/bin/simple" "$release_root/lib/libsimplevm_runtime.a" "$release_root/include/simplevm/vm.h"; do
  if [[ ! -e "$req" ]]; then
    echo "invalid archive: missing $req" >&2
    exit 1
  fi
done

install_root="${PREFIX}/${VERSION}"
rm -rf "$install_root"
mkdir -p "$install_root"
cp -R "$release_root/"* "$install_root/"

mkdir -p "$PREFIX"
ln -sfn "$install_root" "${PREFIX}/current"

if [[ "$NO_LINK" -eq 0 ]]; then
  mkdir -p "$BIN_DIR"
  ln -sfn "${PREFIX}/current/bin/simple" "${BIN_DIR}/simple"
  ln -sfn "${PREFIX}/current/bin/simplevm" "${BIN_DIR}/simplevm"
fi

echo "installed simple to ${install_root}"
echo "active version: ${PREFIX}/current"
if [[ "$NO_LINK" -eq 0 ]]; then
  echo "linked binaries: ${BIN_DIR}/simple and ${BIN_DIR}/simplevm"
  case ":${PATH}:" in
    *":${BIN_DIR}:"*) ;;
    *)
      echo "PATH note: add this to your shell profile:"
      echo "  export PATH=\"${BIN_DIR}:\$PATH\""
      ;;
  esac
fi
