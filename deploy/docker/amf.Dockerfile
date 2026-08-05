# Multi-stage build for nfs/amf. Mirrors deploy/docker/nrf.Dockerfile -- see that file's header
# comment for why /build is used as the source root (nfs/amf/CMakeLists.txt bakes CERTS_DIR in as
# a compile-time absolute path the same way nfs/nrf/CMakeLists.txt does).
#
# UNLIKE nrf.Dockerfile's standalone entrypoint: this image does NOT generate its own lab PKI at
# start. AMF's cert must chain to the SAME root CA nrf's cert does (and vice versa, for mTLS to
# work both directions) -- two containers each independently running gen-lab-pki.sh would mint two
# unrelated CAs, and the mTLS handshake between them would fail. See
# deploy/docker/docker-compose.yml's pki-init service, which provisions certs/ once into a shared
# volume both nrf and amf mount. This entrypoint assumes /build/certs is already populated when the
# container starts.

FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl zip unzip tar pkg-config \
    python3 python3-pip python3-venv ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/codegen-venv && /opt/codegen-venv/bin/pip install jinja2 pyyaml
ENV PATH="/opt/codegen-venv/bin:${PATH}"

RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && git -C /opt/vcpkg checkout f1d4bbc72f183441403ba5107cb19d75a5abc2a2 \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build
COPY . .

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release -D5GC_BUILD_TESTS=OFF \
    && cmake --build build --target amf

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    openssl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY --from=builder /build/build/nfs/amf/amf /build/amf

EXPOSE 7778/tcp 9465/tcp

ENTRYPOINT ["/build/amf"]
