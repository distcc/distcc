#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ROOT_DIR
JOBS="${JOBS:-2}"
readonly JOBS
if [ -n "${PYTHON_BIN:-}" ]; then
  :
elif command -v python3.13 >/dev/null 2>&1; then
  PYTHON_BIN="python3.13"
else
  PYTHON_BIN="python3"
fi
readonly PYTHON_BIN

log() {
  printf '[release-packages] %s\n' "$*"
}

fail() {
  printf '[release-packages] ERROR: %s\n' "$*" >&2
  exit 1
}

main() {
  cd "${ROOT_DIR}"

  command -v "${PYTHON_BIN}" >/dev/null 2>&1 || fail "missing Python interpreter: ${PYTHON_BIN}"
  command -v pkg-config >/dev/null 2>&1 || fail "missing pkg-config"
  command -v eu-strip >/dev/null 2>&1 || fail "missing eu-strip"
  command -v rpmbuild >/dev/null 2>&1 || fail "missing rpmbuild"
  command -v alien >/dev/null 2>&1 || fail "missing alien"
  command -v fakeroot >/dev/null 2>&1 || fail "missing fakeroot"

  log "bootstrapping"
  ./autogen.sh

  log "configuring non-zstd release build"
  ./configure PYTHON="${PYTHON_BIN}" --without-zstd --enable-Werror

  log "building source tarball and binary packages"
  make -j"${JOBS}" deb

  log "package build completed"
}

main "$@"
