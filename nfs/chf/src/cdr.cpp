#include "cdr.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/nullable.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <set>

#include "cdr_asn1.hpp"

namespace chf {

namespace {

template <typename ColumnT, typename ValueT>
clickhouse::ColumnRef nullable_column(const std::optional<ValueT>& value) {
    auto data = std::make_shared<ColumnT>();
    auto nulls = std::make_shared<clickhouse::ColumnUInt8>();
    if (value.has_value()) {
        data->Append(*value);
        nulls->Append(0);
    } else {
        data->Append(ValueT{});
        nulls->Append(1);
    }
    return std::make_shared<clickhouse::ColumnNullable>(data, nulls);
}

} // namespace

CdrWriter::CdrWriter(const clickhouse::ClientOptions& options) {
    try {
        client_ = std::make_unique<clickhouse::Client>(options);
    } catch (const std::exception& e) {
        spdlog::warn("chf: could not connect to ClickHouse, CDR generation disabled: {}", e.what());
        client_.reset();
    }
}

void CdrWriter::write(const CdrRecord& record) {
    if (!client_) {
        spdlog::warn("chf: CDR write skipped for ChargingDataRef={} -- ClickHouse not connected",
                     record.charging_data_ref);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    clickhouse::Block block;

    auto ref_col = std::make_shared<clickhouse::ColumnString>();
    ref_col->Append(record.charging_data_ref);
    block.AppendColumn("charging_data_ref", ref_col);

    auto seq_col = std::make_shared<clickhouse::ColumnInt64>();
    seq_col->Append(record.invocation_sequence_number);
    block.AppendColumn("invocation_sequence_number", seq_col);

    auto service_type_col = std::make_shared<clickhouse::ColumnString>();
    service_type_col->Append(record.service_type);
    block.AppendColumn("service_type", service_type_col);

    auto operation_col = std::make_shared<clickhouse::ColumnString>();
    operation_col->Append(record.operation);
    block.AppendColumn("operation", operation_col);

    auto supi_col = std::make_shared<clickhouse::ColumnString>();
    supi_col->Append(record.subscriber_identifier);
    block.AppendColumn("subscriber_identifier", supi_col);

    auto nf_col = std::make_shared<clickhouse::ColumnString>();
    nf_col->Append(record.nf_consumer_node_functionality);
    block.AppendColumn("nf_consumer_node_functionality", nf_col);

    block.AppendColumn("rating_group",
                       nullable_column<clickhouse::ColumnInt64>(record.rating_group));
    block.AppendColumn("granted_total_volume",
                       nullable_column<clickhouse::ColumnUInt64>(record.granted_total_volume));
    block.AppendColumn(
        "granted_service_specific_units",
        nullable_column<clickhouse::ColumnUInt64>(record.granted_service_specific_units));
    block.AppendColumn("used_total_volume",
                       nullable_column<clickhouse::ColumnUInt64>(record.used_total_volume));
    block.AppendColumn("reserved_cost",
                       nullable_column<clickhouse::ColumnFloat64>(record.reserved_cost));
    block.AppendColumn("reserved_cost_currency",
                       nullable_column<clickhouse::ColumnString>(record.reserved_cost_currency));

    auto ts_col = std::make_shared<clickhouse::ColumnDateTime>();
    ts_col->Append(record.invocation_time_stamp);
    block.AppendColumn("invocation_time_stamp", ts_col);

    // Gap-closure (task #108, ADR-0089): real TS 32.298 ChargingRecord, BER-encoded. Stored as raw
    // bytes in a ClickHouse String column (empty if encode_chf_cdr found no real
    // NetworkFunctionality mapping for this row -- see its own comment).
    const auto asn1_bytes = encode_chf_cdr(record, record.recording_network_function_id);
    auto asn1_col = std::make_shared<clickhouse::ColumnString>();
    asn1_col->Append(std::string(asn1_bytes.begin(), asn1_bytes.end()));
    block.AppendColumn("asn1_cdr", asn1_col);

    client_->Insert("cdr", block);
}

std::vector<std::int64_t> CdrWriter::detect_gaps(const std::string& charging_data_ref) {
    if (!client_) {
        spdlog::warn(
            "chf: CDR gap-detection skipped for ChargingDataRef={} -- ClickHouse not connected",
            charging_data_ref);
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::int64_t> seen;
    clickhouse::Query query(
        "SELECT DISTINCT invocation_sequence_number FROM cdr "
        "WHERE charging_data_ref = {ref:String} ORDER BY invocation_sequence_number");
    query.SetParam("ref", charging_data_ref);
    query.OnData([&seen](const clickhouse::Block& block) {
        if (block.GetRowCount() == 0) {
            return;
        }
        auto col = block[0]->As<clickhouse::ColumnInt64>();
        for (std::size_t i = 0; i < block.GetRowCount(); ++i) {
            seen.insert(col->At(i));
        }
    });

    client_->Select(query);

    std::vector<std::int64_t> gaps;
    if (seen.size() < 2) {
        return gaps;
    }
    const auto lo = *seen.begin();
    const auto hi = *seen.rbegin();
    for (auto v = lo; v <= hi; ++v) {
        if (seen.count(v) == 0) {
            gaps.push_back(v);
        }
    }
    return gaps;
}

} // namespace chf
