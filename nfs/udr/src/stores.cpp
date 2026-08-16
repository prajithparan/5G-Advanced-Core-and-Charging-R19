#include "stores.hpp"

namespace udr {

AmfContextStore::AmfContextStore(const std::string& conninfo) : conn_(conninfo) {}

bool AmfContextStore::put(const std::string& ue_id, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    // xmax = 0 is a real Postgres idiom for "this row was inserted by this command, not updated"
    // -- lets one UPSERT statement report the same 201-vs-204 distinction the in-memory version's
    // separate find()-then-write did.
    const auto row = txn.exec("INSERT INTO udr_amf_context (ue_id, context) VALUES ($1, $2::jsonb) "
                              "ON CONFLICT (ue_id) DO UPDATE SET context = EXCLUDED.context "
                              "RETURNING (xmax = 0) AS inserted",
                              pqxx::params{ue_id, context.dump()})
                         .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> AmfContextStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT context FROM udr_amf_context WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["context"].as<std::string>()));
}

std::optional<nlohmann::json> AmfContextStore::apply_patch(const std::string& ue_id,
                                                           const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT context FROM udr_amf_context WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    auto context = nlohmann::json::parse(result.front()["context"].as<std::string>());
    context = context.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("UPDATE udr_amf_context SET context = $2::jsonb WHERE ue_id = $1",
             pqxx::params{ue_id, context.dump()});
    txn.commit();
    return std::make_optional(context);
}

SmfRegistrationStore::SmfRegistrationStore(const std::string& conninfo) : conn_(conninfo) {}

bool SmfRegistrationStore::put(const std::string& ue_id,
                               const std::string& pdu_session_id,
                               nlohmann::json registration) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row =
        txn.exec("INSERT INTO udr_smf_registration (ue_id, pdu_session_id, registration) "
                 "VALUES ($1, $2, $3::jsonb) "
                 "ON CONFLICT (ue_id, pdu_session_id) DO UPDATE SET registration = "
                 "EXCLUDED.registration "
                 "RETURNING (xmax = 0) AS inserted",
                 pqxx::params{ue_id, pdu_session_id, registration.dump()})
            .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> SmfRegistrationStore::get(const std::string& ue_id,
                                                        const std::string& pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT registration FROM udr_smf_registration "
                                 "WHERE ue_id = $1 AND pdu_session_id = $2",
                                 pqxx::params{ue_id, pdu_session_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(
        nlohmann::json::parse(result.front()["registration"].as<std::string>()));
}

std::optional<nlohmann::json> SmfRegistrationStore::apply_patch(const std::string& ue_id,
                                                                const std::string& pdu_session_id,
                                                                const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT registration FROM udr_smf_registration "
                                 "WHERE ue_id = $1 AND pdu_session_id = $2",
                                 pqxx::params{ue_id, pdu_session_id});
    if (result.empty()) {
        return std::nullopt;
    }
    auto registration = nlohmann::json::parse(result.front()["registration"].as<std::string>());
    registration = registration.patch(patch_ops); // may throw nlohmann::json::exception
    txn.exec("UPDATE udr_smf_registration SET registration = $3::jsonb "
             "WHERE ue_id = $1 AND pdu_session_id = $2",
             pqxx::params{ue_id, pdu_session_id, registration.dump()});
    txn.commit();
    return std::make_optional(registration);
}

bool SmfRegistrationStore::remove(const std::string& ue_id, const std::string& pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("DELETE FROM udr_smf_registration "
                                 "WHERE ue_id = $1 AND pdu_session_id = $2",
                                 pqxx::params{ue_id, pdu_session_id});
    txn.commit();
    return result.affected_rows() > 0;
}

std::vector<nlohmann::json> SmfRegistrationStore::list_for_ue(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT registration FROM udr_smf_registration WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    std::vector<nlohmann::json> out;
    out.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
        out.push_back(nlohmann::json::parse(row["registration"].as<std::string>()));
    }
    return out;
}

} // namespace udr
