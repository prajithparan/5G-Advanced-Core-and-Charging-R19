#!/usr/bin/env bash
# Downloads vanilla asn1c 0.9.29 (BSD-licensed, https://github.com/vlm/asn1c) and builds it to
# build-tools/asn1c/, then applies this project's Aligned PER (X.691) patch to its skeleton
# sources -- see docs/DECISIONS.md ADR-0031 for why: vanilla asn1c only ever shipped Unaligned
# PER, which real gNB/UE peers speaking TS 38.413 Aligned PER cannot decode. The patch is
# self-authored from the X.691 spec text (not copied from any other asn1c fork -- a different
# fork, osmocom/asn1c's aper-prefix branch, was tried first and abandoned: its compiler cannot
# parse the real NGAP-17.9 module, and its runtime skeleton is ABI-incompatible with vanilla
# 0.9.29's newer asn_bit_data_t-based architecture).
#
# build-tools/ is gitignored (matches the existing build-*/ ignore pattern) -- this script is how
# every dev machine and CI reproduces it, since committing a patched compiler's build output isn't
# appropriate for this repo. Idempotent: safe to re-run.
#
# Usage: scripts/setup-asn1c.sh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TOOLS_DIR="${ROOT_DIR}/build-tools"
ASN1C_VERSION="0.9.29"
ASN1C_PREFIX="${BUILD_TOOLS_DIR}/asn1c"
WORK_DIR="${BUILD_TOOLS_DIR}/.asn1c-src"

if [[ -x "${ASN1C_PREFIX}/bin/asn1c" ]]; then
    echo "asn1c already built at ${ASN1C_PREFIX}/bin/asn1c -- remove that directory to rebuild from scratch."
    exit 0
fi

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

if [[ ! -f "asn1c-${ASN1C_VERSION}.tar.gz" ]]; then
    echo "Downloading asn1c ${ASN1C_VERSION} release tarball..."
    curl -fsSL -o "asn1c-${ASN1C_VERSION}.tar.gz" \
        "https://github.com/vlm/asn1c/releases/download/v${ASN1C_VERSION}/asn1c-${ASN1C_VERSION}.tar.gz"
fi

rm -rf "asn1c-${ASN1C_VERSION}"
tar xzf "asn1c-${ASN1C_VERSION}.tar.gz"
cd "asn1c-${ASN1C_VERSION}"

echo "Applying Aligned PER patch (docs/DECISIONS.md ADR-0031)..."
patch -p1 < "${ROOT_DIR}/scripts/patches/asn1c-aligned-per.patch"

echo "Configuring and building..."
./configure --prefix="${ASN1C_PREFIX}"
make -j"$(nproc)"
make install

echo "asn1c ${ASN1C_VERSION} (Aligned PER patched) installed to ${ASN1C_PREFIX}"
