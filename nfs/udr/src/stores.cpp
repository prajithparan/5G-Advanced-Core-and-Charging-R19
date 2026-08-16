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

ProvisionedDataStore::ProvisionedDataStore(const std::string& conninfo) : conn_(conninfo) {}

void ProvisionedDataStore::seed(const std::string& ue_id,
                                const std::string& serving_plmn_id,
                                std::optional<nlohmann::json> am_data,
                                std::optional<nlohmann::json> smf_sel_data,
                                std::optional<nlohmann::json> sm_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_provisioned_data "
             "(ue_id, serving_plmn_id, am_data, smf_sel_data, sm_data) "
             "VALUES ($1, $2, $3::jsonb, $4::jsonb, $5::jsonb) "
             "ON CONFLICT (ue_id, serving_plmn_id) DO UPDATE SET "
             "am_data = EXCLUDED.am_data, smf_sel_data = EXCLUDED.smf_sel_data, "
             "sm_data = EXCLUDED.sm_data",
             pqxx::params{
                 ue_id,
                 serving_plmn_id,
                 am_data.has_value() ? std::optional<std::string>(am_data->dump()) : std::nullopt,
                 smf_sel_data.has_value() ? std::optional<std::string>(smf_sel_data->dump())
                                          : std::nullopt,
                 sm_data.has_value() ? std::optional<std::string>(sm_data->dump()) : std::nullopt});
    txn.commit();
}

namespace {

std::optional<nlohmann::json> get_provisioned_column(pqxx::connection& conn,
                                                     std::mutex& mutex,
                                                     const std::string& column,
                                                     const std::string& ue_id,
                                                     const std::string& serving_plmn_id) {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work txn(conn);
    const auto result = txn.exec(
        "SELECT " + column + " FROM udr_provisioned_data WHERE ue_id = $1 AND serving_plmn_id = $2",
        pqxx::params{ue_id, serving_plmn_id});
    if (result.empty()) {
        return std::nullopt;
    }
    const auto value = result.front()[0].as<std::optional<std::string>>();
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(*value));
}

} // namespace

std::optional<nlohmann::json>
ProvisionedDataStore::get_am_data(const std::string& ue_id, const std::string& serving_plmn_id) {
    return get_provisioned_column(conn_, mutex_, "am_data", ue_id, serving_plmn_id);
}

std::optional<nlohmann::json>
ProvisionedDataStore::get_smf_sel_data(const std::string& ue_id,
                                       const std::string& serving_plmn_id) {
    return get_provisioned_column(conn_, mutex_, "smf_sel_data", ue_id, serving_plmn_id);
}

std::optional<nlohmann::json>
ProvisionedDataStore::get_sm_data(const std::string& ue_id, const std::string& serving_plmn_id) {
    return get_provisioned_column(conn_, mutex_, "sm_data", ue_id, serving_plmn_id);
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

SmPolicyDataStore::SmPolicyDataStore(const std::string& conninfo) : conn_(conninfo) {}

std::optional<nlohmann::json> SmPolicyDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT policy_data FROM udr_sm_policy_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(
        nlohmann::json::parse(result.front()["policy_data"].as<std::string>()));
}

nlohmann::json SmPolicyDataStore::merge_patch(const std::string& ue_id,
                                              const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT policy_data FROM udr_sm_policy_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    // Real, deliberate upsert: an absent document starts from `{}` rather than being an error --
    // see this store's own header comment on why (no real POST exists for this resource, so PATCH
    // is this project's own chosen create path).
    auto doc = result.empty()
                   ? nlohmann::json::object()
                   : nlohmann::json::parse(result.front()["policy_data"].as<std::string>());
    doc.merge_patch(patch);
    txn.exec("INSERT INTO udr_sm_policy_data (ue_id, policy_data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET policy_data = EXCLUDED.policy_data",
             pqxx::params{ue_id, doc.dump()});
    txn.commit();
    return doc;
}

} // namespace udr
