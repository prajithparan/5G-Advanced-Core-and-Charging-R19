# Multi-stage build for bss/product-catalog. Mirrors deploy/docker/pcf.Dockerfile -- see
# amf.Dockerfile's header comment for why this image does NOT generate its own lab PKI at start
# (must share the same root CA as every other NF; see deploy/docker/docker-compose.yml's pki-init
# service). Not a 3GPP NF (see bss/product-catalog/src/main.cpp's own header comment), but built
# the same way as one for consistency with this repo's existing image convention.

FROM ubuntu:24.04 AS builder

# bison/flex: vcpkg builds libpq from source (libpqxx, ADR-0054 -- this service's own real
# PostgreSQL persistence, the direct reason this image exists in this form).
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl zip unzip tar pkg-config \
    python3 python3-pip python3-venv ca-certificates bison flex patch \
    # libsctp-dev/libbpf-dev/libcap-dev/clang-18: libs/ngap-core (SCTP) and nfs/upf
    # (eBPF/XDP datapath) require these at CMake configure time -- same real,
    # pre-existing gap as the asn1c one above, matching the fix
    # .github/workflows/ci.yml already needed for the identical reason.
    libsctp-dev libbpf-dev libcap-dev clang-18 \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/codegen-venv && /opt/codegen-venv/bin/pip install jinja2 pyyaml
ENV PATH="/opt/codegen-venv/bin:${PATH}"

RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && git -C /opt/vcpkg checkout f1d4bbc72f183441403ba5107cb19d75a5abc2a2 \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build
COPY . .

# asn1c: libs/ngap-generated needs this real toolchain at configure time (ADR-0030/
# ADR-0031) -- pre-existing gap, independent of the libpqxx/bison fix above: this repo's
# Dockerfiles never ran this step, so a from-scratch image build was already broken
# before product-catalog/libpqxx existed. Found by actually running a real docker build,
# not assumed.
RUN ./scripts/setup-asn1c.sh

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release -D5GC_BUILD_TESTS=OFF \
    && cmake --build build --target product-catalog

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    openssl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY --from=builder /build/build/bss/product-catalog/product-catalog /build/product-catalog

EXPOSE 7785/tcp 9473/tcp

ENTRYPOINT ["/build/product-catalog"]
