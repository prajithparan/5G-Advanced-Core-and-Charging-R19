#!/usr/bin/env bash
# ADR-0263: reproduce this project's first measured SBI baseline.
#
# What this measures and what it does NOT:
#   * It measures NRF NFDiscovery (SearchNFInstances) -- the hottest SBI path in a real core, and
#     the one whose full TS 28.552 5.10.3 measurement family ADR-0262 instrumented.
#   * It is a BASELINE of this build on this machine. It is NOT ADR-0238 step (4): no comparison
#     against free5GC or anything else is performed, and none is implied.
#   * The load generator and the system under test share one host, over loopback. That inflates
#     latency and caps throughput. The numbers characterise this machine, not the design.
#   * ADR-0009's synchronous HTTP client is still open. ADR-0238 sequenced the client fix BEFORE
#     benchmarking precisely because it distorts throughput; this baseline is taken with it open,
#     deliberately and disclosed.
#
# Usage: scripts/run-baseline-benchmark.sh [output-dir]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/build/benchmark-results}"
mkdir -p "$OUT"

CERTS="$ROOT/certs"
NRF="$ROOT/build/nfs/nrf/nrf"
UDM="$ROOT/build/nfs/udm/udm"
LOADGEN="$ROOT/build/tools/sbi-loadgen/sbi-loadgen"

for bin in "$NRF" "$UDM" "$LOADGEN"; do
    [ -x "$bin" ] || { echo "missing $bin -- build first" >&2; exit 1; }
done

echo "== environment =="
{
    echo "date:    $(date -Is)"
    echo "kernel:  $(uname -sr)"
    echo "cpu:     $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "cores:   $(nproc)"
    echo "memory:  $(awk '/MemTotal/{printf "%.1f GiB", $2/1048576}' /proc/meminfo)"
    echo "commit:  $(cd "$ROOT" && git rev-parse --short HEAD)"
    echo "note:    load generator and SUT share this host; all traffic over loopback"
} | tee "$OUT/environment.txt"

cleanup() { [ -n "${NRF_PID:-}" ] && kill "$NRF_PID" 2>/dev/null || true
            [ -n "${UDM_PID:-}" ] && kill "$UDM_PID" 2>/dev/null || true; }
trap cleanup EXIT

"$NRF" >"$OUT/nrf.log" 2>&1 & NRF_PID=$!
sleep 2
"$UDM" >"$OUT/udm.log" 2>&1 & UDM_PID=$!   # gives discovery a real profile to return
sleep 2

# A FRESH token per run, deliberately. The first version of this script fetched one token up
# front; a later run then drifted past its 3600 s lifetime and every request in it came back 401,
# so the measurement recorded the rejection path at full speed instead of real discovery. The
# numbers looked plausible -- which is exactly why the guard below exists rather than a comment
# saying "remember to check". Every run now re-authenticates and then ASSERTS that the run it just
# completed actually returned 200s.
fetch_token() {
    curl -s --http2-prior-knowledge \
        --cert "$CERTS/hello-nf/cert.pem" --key "$CERTS/hello-nf/key.pem" --cacert "$CERTS/ca/ca.crt" \
        -X POST "https://127.0.0.1:7777/oauth2/token" \
        -H "content-type: application/x-www-form-urlencoded" \
        -d "grant_type=client_credentials&nfInstanceId=bench&scope=nnrf-disc&targetNfType=NRF" \
        | python3 -c 'import sys,json; print(json.load(sys.stdin)["access_token"])'
}

URL="https://127.0.0.1:7777/nnrf-disc/v1/nf-instances?target-nf-type=UDM"

# Fails the whole run if the result file records any non-200 status. A benchmark that silently
# measured error responses is worse than no benchmark.
assert_all_200() {
    local file="$1"
    if ! grep -qE '^status codes *: 200=' "$file" || grep -qE '^status codes *:.*(401|403|404|500)=' "$file"; then
        echo "REFUSING TO REPORT: $file did not record an all-200 run:" >&2
        grep -E '^status codes' "$file" >&2
        exit 1
    fi
}

run_case() {
    local label="$1"; shift
    echo "== $label ($(date +%H:%M:%S)) =="
    local token; token="$(fetch_token)"
    [ -n "$token" ] || { echo "failed to obtain a token" >&2; exit 1; }
    "$LOADGEN" --url "$URL" --cert "$CERTS/hello-nf/cert.pem" --key "$CERTS/hello-nf/key.pem" \
        --ca "$CERTS/ca/ca.crt" --header "authorization: Bearer $token" \
        --warmup 2 --duration 15 "$@" --json "$OUT/$label.json" | tee "$OUT/$label.txt"
    assert_all_200 "$OUT/$label.txt"
}

for c in 1 8 32; do
    run_case "closed-c$c" --concurrency "$c"
done

run_case "open-500rps" --concurrency 32 --rate 500

echo "== TS 28.552 counters after the run (ADR-0262) =="
curl -s http://127.0.0.1:9464/metrics | grep -E '^nrf_nfs_' | tee "$OUT/nrf-28552-counters.txt"

echo
echo "results written to $OUT"
