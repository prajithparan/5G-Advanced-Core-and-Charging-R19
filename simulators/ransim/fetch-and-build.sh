#!/usr/bin/env bash
# Fetches and builds UERANSIM (AGPL-3.0, github.com/aligungr/UERANSIM) at a pinned commit.
# See docs/DECISIONS.md ADR-0016: fetched on demand, never vendored into this repository's
# git history, run only as an arms-length external process.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENDOR_DIR="${SCRIPT_DIR}/vendor/UERANSIM"
REPO_URL="https://github.com/aligungr/UERANSIM.git"
PINNED_COMMIT="6bf5a1a96aaef6ae8778b9d8b477ac6e2bbf8156"  # tag v3.3.0

if [ -d "${VENDOR_DIR}" ]; then
  echo "ransim: ${VENDOR_DIR} already exists, skipping clone (delete it to re-fetch)."
else
  echo "ransim: cloning UERANSIM @ ${PINNED_COMMIT}..."
  git clone "${REPO_URL}" "${VENDOR_DIR}"
  git -C "${VENDOR_DIR}" checkout "${PINNED_COMMIT}"
fi

echo "ransim: building (cmake + make)..."
cd "${VENDOR_DIR}"
make build

echo "ransim: build complete -- binaries in ${VENDOR_DIR}/build/"
