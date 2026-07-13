#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *)
    echo "scripts/build/windows.sh can only run on Windows hosts (Git Bash/MSYS/Cygwin)." >&2
    exit 1
    ;;
esac

VERSION="${VERSION:-$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")}"
RUN_TESTS=0
SKIP_BUILD=0
SKIP_RELEASE=0
SKIP_INSTALL=0
NO_LINK=0
ADD_PATH=0
PREFIX="${LOCALAPPDATA:-$HOME/AppData/Local}/Simple"
BIN_DIR="${PREFIX}/bin"
CMAKE_ARGS=()
ARCH="${PROCESSOR_ARCHITECTURE:-x86_64}"
case "$ARCH" in
  AMD64|x86_64|amd64) ARCH="x86_64" ;;
  ARM64|arm64|aarch64) ARCH="arm64" ;;
  x86|i686) ARCH="x86" ;;
esac
TARGET="windows-${ARCH}"

usage() {
  cat <<'EOT'
Usage: ./scripts/build/windows.sh [options]
Options:
  --version <name>
  --prefix <dir>
  --bin-dir <dir>
  --tests | --no-tests
  --skip-build
  --skip-release
  --skip-install
  --no-link
  --add-path
  --cmake-arg <arg>
EOT
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    --prefix) PREFIX="${2:-}"; shift 2 ;;
    --bin-dir) BIN_DIR="${2:-}"; shift 2 ;;
    --tests) RUN_TESTS=1; shift ;;
    --no-tests) RUN_TESTS=0; shift ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --skip-release) SKIP_RELEASE=1; shift ;;
    --skip-install) SKIP_INSTALL=1; shift ;;
    --no-link) NO_LINK=1; shift ;;
    --add-path) ADD_PATH=1; shift ;;
    --cmake-arg) CMAKE_ARGS+=("${2:-}"); shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

BUILD_DIR="$ROOT_DIR/build"
DIST_DIR="$ROOT_DIR/dist"
STAGE_DIR="$DIST_DIR/simple-${VERSION}-${TARGET}"
PKG_PATH="$DIST_DIR/simple-${VERSION}-${TARGET}.zip"
BIN_OUT="$BUILD_DIR/bin"
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"

if [[ -f "$CACHE_FILE" ]]; then
  cached_source="$(grep '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$CACHE_FILE" 2>/dev/null | cut -d= -f2- || true)"
  if [[ -n "$cached_source" && "$cached_source" != "$ROOT_DIR" ]]; then
    echo "Removing stale CMake cache from previous source path: $cached_source"
    rm -rf "$BUILD_DIR"
  fi
fi

find_exe() {
  local name="$1"
  for p in "$BIN_OUT/${name}.exe" "$BIN_OUT/Release/${name}.exe" "$BUILD_DIR/Release/${name}.exe"; do
    [[ -f "$p" ]] && { printf '%s' "$p"; return 0; }
  done
  return 1
}

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  if [[ "${#CMAKE_ARGS[@]}" -gt 0 ]]; then
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
      -DSIMPLEVM_VERSION_OVERRIDE="$VERSION" \
      "${CMAKE_ARGS[@]}"
  else
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
      -DSIMPLEVM_VERSION_OVERRIDE="$VERSION"
  fi
  cmake --build "$BUILD_DIR" --config Release --target simplevm_runtime_static simplevm simple_stub --parallel "${JOBS:-2}"
  svm_exe="$(find_exe svm)"
  simple_exe="$(find_exe simple)"
  rm -rf "$ROOT_DIR/bin"
  mkdir -p "$ROOT_DIR/bin"
  cp "$svm_exe" "$ROOT_DIR/bin/svm.exe"
  cp "$simple_exe" "$ROOT_DIR/bin/simple.exe"
  if [[ "$RUN_TESTS" -eq 1 ]]; then
    cmake --build "$BUILD_DIR" --config Release --target simplevm_tests --parallel "${JOBS:-2}"
    test_exe="$(find_exe simplevm_tests)"
    "$test_exe"
  fi
fi

if [[ "$SKIP_RELEASE" -eq 0 ]]; then
  mkdir -p "$DIST_DIR"
  rm -rf "$STAGE_DIR"
  mkdir -p "$STAGE_DIR/bin" "$STAGE_DIR/lib" "$STAGE_DIR/include/simplevm" "$STAGE_DIR/share/simple"
  cp "$ROOT_DIR/bin/svm.exe" "$STAGE_DIR/bin/svm.exe"
  cp "$ROOT_DIR/bin/simple.exe" "$STAGE_DIR/bin/simple.exe"
  runtime_lib=""
  for candidate in "$BIN_OUT/simplevm_runtime.lib" "$BIN_OUT/Release/simplevm_runtime.lib"; do
    [[ -f "$candidate" ]] && runtime_lib="$candidate" && break
  done
  [[ -n "$runtime_lib" ]] || { echo "static Simple runtime library not found" >&2; exit 1; }
  cp "$runtime_lib" "$STAGE_DIR/lib/simplevm_runtime.lib"
  ffi_lib=""
  for candidate in "$BIN_OUT/ffi.lib" "$BIN_OUT/Release/ffi.lib"; do
    [[ -f "$candidate" ]] && ffi_lib="$candidate" && break
  done
  [[ -n "$ffi_lib" ]] || { echo "static libffi library not found" >&2; exit 1; }
  cp "$ffi_lib" "$STAGE_DIR/lib/ffi.lib"
  cp -R "$ROOT_DIR/source/VM/include/." "$STAGE_DIR/include/simplevm/"
  cp -R "$ROOT_DIR/source/Byte/include/." "$STAGE_DIR/include/simplevm/"
  [[ -f "$ROOT_DIR/docs/README.md" ]] && cp "$ROOT_DIR/docs/README.md" "$STAGE_DIR/share/simple/README.md"
  rm -f "$PKG_PATH"
  if command -v powershell.exe >/dev/null 2>&1; then
    src_win="$STAGE_DIR"
    dst_win="$PKG_PATH"
    if command -v cygpath >/dev/null 2>&1; then
      src_win="$(cygpath -w "$STAGE_DIR")"
      dst_win="$(cygpath -w "$PKG_PATH")"
    fi
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '${src_win}\\*' -DestinationPath '${dst_win}' -Force" >/dev/null
  elif command -v zip >/dev/null 2>&1; then
    (cd "$DIST_DIR" && zip -qr "$(basename "$PKG_PATH")" "$(basename "$STAGE_DIR")")
  else
    echo "zip packager not found; stage kept at $STAGE_DIR" >&2
  fi
fi

if [[ "$SKIP_INSTALL" -eq 0 ]]; then
  install_root="${PREFIX}/${VERSION}"
  rm -rf "$install_root"
  mkdir -p "$install_root"
  cp -R "$STAGE_DIR/"* "$install_root/"
  rm -rf "${PREFIX}/current"
  cp -R "$install_root" "${PREFIX}/current"
  if [[ "$NO_LINK" -eq 0 ]]; then
    mkdir -p "$BIN_DIR"
    cp -f "${PREFIX}/current/bin/svm.exe" "$BIN_DIR/svm.exe"
    cp -f "${PREFIX}/current/bin/simple.exe" "$BIN_DIR/simple.exe"
  fi
  if [[ "$ADD_PATH" -eq 1 && "$NO_LINK" -eq 0 ]] && command -v powershell.exe >/dev/null 2>&1; then
    bin_win="$BIN_DIR"
    command -v cygpath >/dev/null 2>&1 && bin_win="$(cygpath -w "$BIN_DIR")"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command '$p=[Environment]::GetEnvironmentVariable("Path","User"); if(-not $p){$p=""}; $arr=$p -split ";" | ?{$_}; if($arr -notcontains "'"$bin_win"'"){ [Environment]::SetEnvironmentVariable("Path", ($p + ($(if($p){";"}else{""})) + "'"$bin_win"'"), "User") }' >/dev/null
  fi
fi

echo "Windows build complete"
echo "target: $TARGET"
echo "version: $VERSION"
