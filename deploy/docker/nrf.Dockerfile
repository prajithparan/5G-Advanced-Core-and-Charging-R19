# Multi-stage build for nfs/nrf. Both stages use /build as the source root because
# nfs/nrf/CMakeLists.txt bakes CERTS_DIR in as a compile-time absolute path
# (${CMAKE_SOURCE_DIR}/certs, see docs/DECISIONS.md ADR-0011/ADR-0014) -- the runtime stage's
# entrypoint generates the lab PKI fresh at that same path so the compiled-in path resolves.
# This is a direct, disclosed consequence of that earlier simplification, not hidden here.

FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl zip unzip tar pkg-config \
    python3 python3-pip python3-venv ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/codegen-venv && /opt/codegen-venv/bin/pip install jinja2 pyyaml
ENV PATH="/opt/codegen-venv/bin:${PATH}"

# Full (non-shallow) clone + explicit checkout of the exact commit vcpkg.json's builtin-baseline
# pins: a shallow clone only fetches the default branch's current tip, which usually does NOT
# contain that specific historical commit object, and vcpkg's version resolution needs it present.
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && git -C /opt/vcpkg checkout f1d4bbc72f183441403ba5107cb19d75a5abc2a2 \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build
COPY . .

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release -D5GC_BUILD_TESTS=OFF \
    && cmake --build build --target nrf

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    openssl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY --from=builder /build/build/nfs/nrf/nrf /build/nrf
COPY --from=builder /build/scripts/gen-lab-pki.sh /build/scripts/gen-lab-pki.sh

EXPOSE 7777/tcp 9464/tcp

# Generates a fresh lab CA + nrf cert + JWT signing key on first start (see
# scripts/gen-lab-pki.sh) -- consistent with this being a lab image, not a real deployment with a
# provisioned PKI. Mount /build/certs as a volume to persist across restarts if that matters for a
# given lab run; ephemeral-per-container is the default and is fine for most uses.
ENTRYPOINT ["/bin/bash", "-c", "./scripts/gen-lab-pki.sh nrf && exec ./nrf"]
