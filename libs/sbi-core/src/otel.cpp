#include "sbi_core/otel.hpp"

#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>

namespace sbi_core {

void init_tracing(const std::string& service_name, const std::string& otlp_endpoint) {
    namespace trace_sdk = opentelemetry::sdk::trace;
    namespace trace_api = opentelemetry::trace;
    namespace resource = opentelemetry::sdk::resource;

    std::unique_ptr<trace_sdk::SpanExporter> exporter;
    std::unique_ptr<trace_sdk::SpanProcessor> processor;

    if (otlp_endpoint.empty()) {
        exporter = opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create();
        processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
    } else {
        opentelemetry::exporter::otlp::OtlpHttpExporterOptions opts;
        opts.url = otlp_endpoint;
        exporter = opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create(opts);
        processor = trace_sdk::BatchSpanProcessorFactory::Create(std::move(exporter), {});
    }

    auto resource_attrs = resource::Resource::Create({{"service.name", service_name}});
    auto provider = trace_sdk::TracerProviderFactory::Create(std::move(processor), resource_attrs);
    // Routed through std::shared_ptr as an intermediate: nostd::shared_ptr's constructor overload
    // set is ambiguous when built directly from a std::unique_ptr (it has candidates for both
    // std::unique_ptr&& and nostd::unique_ptr&&, and unique_ptr converts to both).
    std::shared_ptr<trace_api::TracerProvider> shared_provider(std::move(provider));
    trace_api::Provider::SetTracerProvider(
        opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(shared_provider));
}

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer() {
    auto provider = opentelemetry::trace::Provider::GetTracerProvider();
    return provider->GetTracer("sbi-core");
}

} // namespace sbi_core
