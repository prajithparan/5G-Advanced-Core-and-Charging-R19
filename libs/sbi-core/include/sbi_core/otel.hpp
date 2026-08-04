#pragma once

#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/tracer.h>

#include <string>

// Thin wrapper around the OpenTelemetry C++ SDK tracer provider setup. Call init_tracing() once at
// NF startup, then get_tracer() anywhere to start spans with the standard OTel API
// (tracer->StartSpan(name), opentelemetry::trace::Scope).
//
// Lab default: if otlp_endpoint is empty, spans are written to stdout via the OStream exporter
// instead of OTLP/HTTP, so hello-nf works without a collector running. This is a Phase 0
// simplification -- real OTLP export is a one-line change to the exporter factory call in
// otel.cpp once a collector is part of the lab compose stack.

namespace sbi_core {

void init_tracing(const std::string& service_name, const std::string& otlp_endpoint = "");

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer();

} // namespace sbi_core
