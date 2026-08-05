#pragma once

#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/nostd/shared_ptr.h>

#include <cstdint>
#include <string>

// Prometheus metrics via OpenTelemetry C++'s own Prometheus exporter (opentelemetry-cpp[prometheus]
// feature), rather than adding a separate prometheus-cpp dependency -- opentelemetry-cpp is already
// in the stack for tracing (otel.hpp), and its Prometheus exporter reuses prometheus-cpp internally
// as an implementation detail without a second top-level dependency to configure. See
// docs/DECISIONS.md ADR-0013.
//
// Pull model: init_metrics starts a small HTTP listener (civetweb, bundled by the exporter) that
// Prometheus scrapes directly -- there is no push/collector step, unlike otel.hpp's tracing export.

namespace sbi_core {

// bind_address e.g. "127.0.0.1:9464" -- Prometheus scrapes GET http://<bind_address>/metrics.
void init_metrics(const std::string& bind_address);

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> get_meter(const std::string& name);

} // namespace sbi_core
