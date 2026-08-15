# Multi-stage build for nfs/chf. Mirrors deploy/docker/product-catalog.Dockerfile -- see
# amf.Dockerfile's header comment for why this image does NOT generate its own lab PKI at start
# (must share the same root CA as every other NF; see deploy/docker/docker-compose.yml's pki-init
# service). Real, disclosed gap this Dockerfile fixes: CHF has been buildable (`ninja chf`) and
# live-verified manually since P4.3, but had no Dockerfile/compose entry until P4.5/ADR-0061's
# docker-compose.yml fix -- the lab could not actually bring CHF up before this.

FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl zip unzip tar pkg-config \
    python3 python3-pip python3-venv ca-certificates bison flex patch \
    libsctp-dev libbpf-dev libcap-dev clang-18 \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/codegen-venv && /opt/codegen-venv/bin/pip install jinja2 pyyaml
ENV PATH="/opt/codegen-venv/bin:${PATH}"

RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && git -C /opt/vcpkg checkout f1d4bbc72f183441403ba5107cb19d75a5abc2a2 \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build
COPY . .

RUN ./scripts/setup-asn1c.sh

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release -D5GC_BUILD_TESTS=OFF \
    && cmake --build build --target chf

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    openssl ca-certificates libsctp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY --from=builder /build/build/nfs/chf/chf /build/chf

# 7784/tcp: Nchf_ConvergedCharging + Nchf_OfflineOnlyCharging + Nchf_SpendingLimitControl SBI
#           (TLS 1.3 + mTLS)
# 3868/tcp: Diameter Gy + Rf + Sy (ADR-0059)
# 2905/sctp: CAP (TS 29.078) gsmSCF, real M3UA/SCTP port (ADR-0061)
# 9472/tcp: Prometheus /metrics
EXPOSE 7784/tcp 3868/tcp 2905/sctp 9472/tcp

ENTRYPOINT ["/build/chf"]
