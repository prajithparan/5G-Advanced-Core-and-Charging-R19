#include "sbi_core/metrics.hpp"

#include <opentelemetry/exporters/prometheus/exporter_factory.h>
#include <opentelemetry/exporters/prometheus/exporter_options.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>

namespace sbi_core {

void init_metrics(const std::string& bind_address) {
    namespace metrics_exporter = opentelemetry::exporter::metrics;
    namespace metrics_sdk = opentelemetry::sdk::metrics;
    namespace metrics_api = opentelemetry::metrics;

    metrics_exporter::PrometheusExporterOptions opts;
    opts.url = bind_address;
    auto exporter = metrics_exporter::PrometheusExporterFactory::Create(opts);

    auto provider = metrics_sdk::MeterProviderFactory::Create();
    auto* sdk_provider = static_cast<metrics_sdk::MeterProvider*>(provider.get());
    sdk_provider->AddMetricReader(std::move(exporter));

    // See otel.cpp's init_tracing for why this goes through std::shared_ptr first:
    // nostd::shared_ptr has an ambiguous overload set when constructed directly from a
    // std::unique_ptr.
    std::shared_ptr<metrics_api::MeterProvider> shared_provider(std::move(provider));
    metrics_api::Provider::SetMeterProvider(
        opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider>(shared_provider));
}

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> get_meter(const std::string& name) {
    auto provider = opentelemetry::metrics::Provider::GetMeterProvider();
    return provider->GetMeter(name);
}

} // namespace sbi_core
