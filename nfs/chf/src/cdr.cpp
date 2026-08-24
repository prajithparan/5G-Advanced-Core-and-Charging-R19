#include "cdr.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <set>
#include <sstream>

#include "cdr_asn1.hpp"

namespace chf {

namespace {

// Real, standard hex encoding -- see this file's own header comment for why (Doris has no native
// BLOB type, ADR-0192).
std::string hex_encode(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto b : bytes) {
        out.push_back(kHexDigits[(b >> 4) & 0x0F]);
        out.push_back(kHexDigits[b & 0x0F]);
    }
    return out;
}

// mysql_real_escape_string needs a worst-case buffer of 2*len+1 -- the real, documented libmariadb
// contract, not a guessed size.
std::string escape(MYSQL* conn, const std::string& value) {
    std::string out(value.size() * 2 + 1, '\0');
    const auto written =
        mysql_real_escape_string(conn, out.data(), value.c_str(), static_cast<unsigned long>(value.size()));
    out.resize(written);
    return out;
}

std::string sql_or_null(const std::optional<std::int64_t>& value) {
    return value.has_value() ? std::to_string(*value) : "NULL";
}

std::string sql_or_null(const std::optional<std::uint64_t>& value) {
    return value.has_value() ? std::to_string(*value) : "NULL";
}

std::string sql_or_null(const std::optional<double>& value) {
    return value.has_value() ? std::to_string(*value) : "NULL";
}

std::string sql_string_or_null(MYSQL* conn, const std::optional<std::string>& value) {
    return value.has_value() ? "'" + escape(conn, *value) + "'" : "NULL";
}

} // namespace

CdrWriter::CdrWriter(const DorisOptions& options) {
    MYSQL* handle = mysql_init(nullptr);
    if (handle == nullptr) {
        spdlog::warn("chf: mysql_init failed, CDR generation disabled");
        return;
    }
    // Real, disclosed simplification found via live verification (root-caused by reading
    // libmariadb's own vendored source, plugins/auth/my_auth.c: MYSQL_OPT_SSL_VERIFY_SERVER_CERT
    // defaults to "verify required", and that alone -- independent of MYSQL_OPT_SSL_ENFORCE --
    // forces use_ssl=1 during the auth handshake). This project's real Doris deployment (the
    // official apache/doris all-in-one image, docker-compose.yml) has no TLS configured on its FE
    // MySQL port, so connecting without disabling both options fails with "SSL is required, but
    // the server does not support it" even though MYSQL_OPT_SSL_ENFORCE alone is off. Both
    // disabled here, consistent with this project's other backend datastore connections
    // (PostgreSQL, Redis/Valkey) which also run over plaintext in this lab -- the real SBI mTLS
    // discipline (TS 33.501) applies to inter-NF traffic, not this backend link. Real, tracked
    // debt alongside ADR-0009's existing TLS gaps, not a new one.
    my_bool ssl_enforce = 0;
    mysql_options(handle, MYSQL_OPT_SSL_ENFORCE, &ssl_enforce);
    my_bool ssl_verify = 0;
    mysql_options(handle, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &ssl_verify);
    if (mysql_real_connect(handle,
                           options.host.c_str(),
                           options.user.c_str(),
                           options.password.c_str(),
                           options.database.c_str(),
                           options.port,
                           nullptr,
                           0) == nullptr) {
        spdlog::warn("chf: could not connect to Doris, CDR generation disabled: {}",
                     mysql_error(handle));
        mysql_close(handle);
        return;
    }
    conn_ = handle;
}

CdrWriter::~CdrWriter() {
    if (conn_ != nullptr) {
        mysql_close(conn_);
    }
}

void CdrWriter::write(const CdrRecord& record) {
    if (conn_ == nullptr) {
        spdlog::warn("chf: CDR write skipped for ChargingDataRef={} -- Doris not connected",
                     record.charging_data_ref);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Gap-closure (task #108, ADR-0089): real TS 32.298 ChargingRecord, BER-encoded, hex-encoded
    // for storage (ADR-0192, Doris has no BLOB type -- see this file's own header).
    const auto asn1_bytes = encode_chf_cdr(record, record.recording_network_function_id);
    const auto asn1_hex = hex_encode(asn1_bytes);

    char ts_buf[32];
    std::strftime(
        ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&record.invocation_time_stamp));

    std::ostringstream sql;
    sql << "INSERT INTO cdr (charging_data_ref, invocation_sequence_number, service_type, "
           "operation, subscriber_identifier, nf_consumer_node_functionality, rating_group, "
           "granted_total_volume, granted_service_specific_units, used_total_volume, "
           "reserved_cost, reserved_cost_currency, invocation_time_stamp, asn1_cdr) VALUES ('"
        << escape(conn_, record.charging_data_ref) << "', " << record.invocation_sequence_number
        << ", '" << escape(conn_, record.service_type) << "', '" << escape(conn_, record.operation)
        << "', '" << escape(conn_, record.subscriber_identifier) << "', '"
        << escape(conn_, record.nf_consumer_node_functionality) << "', "
        << sql_or_null(record.rating_group) << ", " << sql_or_null(record.granted_total_volume)
        << ", " << sql_or_null(record.granted_service_specific_units) << ", "
        << sql_or_null(record.used_total_volume) << ", " << sql_or_null(record.reserved_cost)
        << ", " << sql_string_or_null(conn_, record.reserved_cost_currency) << ", '" << ts_buf
        << "', '" << asn1_hex << "')";

    const auto query = sql.str();
    if (mysql_real_query(conn_, query.c_str(), static_cast<unsigned long>(query.size())) != 0) {
        spdlog::warn("chf: CDR write to Doris failed for ChargingDataRef={}: {}",
                     record.charging_data_ref,
                     mysql_error(conn_));
    }
}

std::vector<std::int64_t> CdrWriter::detect_gaps(const std::string& charging_data_ref) {
    if (conn_ == nullptr) {
        spdlog::warn("chf: CDR gap-detection skipped for ChargingDataRef={} -- Doris not connected",
                     charging_data_ref);
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::int64_t> seen;

    const std::string query = "SELECT DISTINCT invocation_sequence_number FROM cdr WHERE "
                              "charging_data_ref = '" +
                              escape(conn_, charging_data_ref) +
                              "' ORDER BY invocation_sequence_number";
    if (mysql_real_query(conn_, query.c_str(), static_cast<unsigned long>(query.size())) != 0) {
        spdlog::warn("chf: CDR gap-detection query failed for ChargingDataRef={}: {}",
                     charging_data_ref,
                     mysql_error(conn_));
        return {};
    }

    MYSQL_RES* result = mysql_store_result(conn_);
    if (result == nullptr) {
        return {};
    }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        if (row[0] != nullptr) {
            seen.insert(std::stoll(row[0]));
        }
    }
    mysql_free_result(result);

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
