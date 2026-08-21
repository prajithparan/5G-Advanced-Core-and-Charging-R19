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

AmfNon3GppContextStore::AmfNon3GppContextStore(const std::string& conninfo) : conn_(conninfo) {}

bool AmfNon3GppContextStore::put(const std::string& ue_id, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row =
        txn.exec("INSERT INTO udr_amf_non3gpp_context (ue_id, context) VALUES ($1, $2::jsonb) "
                 "ON CONFLICT (ue_id) DO UPDATE SET context = EXCLUDED.context "
                 "RETURNING (xmax = 0) AS inserted",
                 pqxx::params{ue_id, context.dump()})
            .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> AmfNon3GppContextStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT context FROM udr_amf_non3gpp_context WHERE ue_id = $1",
                                 pqxx::params{ue_id});
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
                                std::optional<nlohmann::json> sm_data,
                                std::optional<nlohmann::json> lcs_bca_data,
                                std::optional<nlohmann::json> sms_mng_data,
                                std::optional<nlohmann::json> sms_data,
                                std::optional<nlohmann::json> trace_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_provisioned_data "
             "(ue_id, serving_plmn_id, am_data, smf_sel_data, sm_data, lcs_bca_data, "
             "sms_mng_data, sms_data, trace_data) "
             "VALUES ($1, $2, $3::jsonb, $4::jsonb, $5::jsonb, $6::jsonb, $7::jsonb, $8::jsonb, "
             "$9::jsonb) "
             "ON CONFLICT (ue_id, serving_plmn_id) DO UPDATE SET "
             "am_data = EXCLUDED.am_data, smf_sel_data = EXCLUDED.smf_sel_data, "
             "sm_data = EXCLUDED.sm_data, lcs_bca_data = EXCLUDED.lcs_bca_data, "
             "sms_mng_data = EXCLUDED.sms_mng_data, sms_data = EXCLUDED.sms_data, "
             "trace_data = EXCLUDED.trace_data",
             pqxx::params{
                 ue_id,
                 serving_plmn_id,
                 am_data.has_value() ? std::optional<std::string>(am_data->dump()) : std::nullopt,
                 smf_sel_data.has_value() ? std::optional<std::string>(smf_sel_data->dump())
                                          : std::nullopt,
                 sm_data.has_value() ? std::optional<std::string>(sm_data->dump()) : std::nullopt,
                 lcs_bca_data.has_value() ? std::optional<std::string>(lcs_bca_data->dump())
                                          : std::nullopt,
                 sms_mng_data.has_value() ? std::optional<std::string>(sms_mng_data->dump())
                                          : std::nullopt,
                 sms_data.has_value() ? std::optional<std::string>(sms_data->dump()) : std::nullopt,
                 trace_data.has_value() ? std::optional<std::string>(trace_data->dump())
                                        : std::nullopt});
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

std::optional<nlohmann::json>
ProvisionedDataStore::get_lcs_bca_data(const std::string& ue_id,
                                       const std::string& serving_plmn_id) {
    return get_provisioned_column(conn_, mutex_, "lcs_bca_data", ue_id, serving_plmn_id);
}

std::optional<nlohmann::json>
ProvisionedDataStore::get_sms_mng_data(const std::string& ue_id,
                                       const std::string& serving_plmn_id) {
    return get_provisioned_column(conn_, mutex_, "sms_mng_data", ue_id, serving_plmn_id);
}

std::optional<nlohmann::json>
ProvisionedDataStore::get_sms_data(const std::string& ue_id, const std::string& serving_plmn_id) {
    return get_provisioned_column(conn_, mutex_, "sms_data", ue_id, serving_plmn_id);
}

std::optional<nlohmann::json>
ProvisionedDataStore::get_trace_data(const std::string& ue_id, const std::string& serving_plmn_id) {
    return get_provisioned_column(conn_, mutex_, "trace_data", ue_id, serving_plmn_id);
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

AuthenticationSubscriptionDataStore::AuthenticationSubscriptionDataStore(
    const std::string& conninfo)
    : conn_(conninfo) {}

std::optional<nlohmann::json> AuthenticationSubscriptionDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec(
        "SELECT data FROM udr_authentication_subscription WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

nlohmann::json AuthenticationSubscriptionDataStore::apply_patch(const std::string& ue_id,
                                                                const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec(
        "SELECT data FROM udr_authentication_subscription WHERE ue_id = $1", pqxx::params{ue_id});
    auto doc = result.empty() ? nlohmann::json::object()
                              : nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc = doc.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("INSERT INTO udr_authentication_subscription (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, doc.dump()});
    txn.commit();
    return doc;
}

AuthenticationStatusStore::AuthenticationStatusStore(const std::string& conninfo)
    : conn_(conninfo) {}

void AuthenticationStatusStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_authentication_status (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> AuthenticationStatusStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_authentication_status WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

bool AuthenticationStatusStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("DELETE FROM udr_authentication_status WHERE ue_id = $1", pqxx::params{ue_id});
    txn.commit();
    return result.affected_rows() > 0;
}

AmPolicyDataStore::AmPolicyDataStore(const std::string& conninfo) : conn_(conninfo) {}

std::optional<nlohmann::json> AmPolicyDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT policy_data FROM udr_am_policy_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(
        nlohmann::json::parse(result.front()["policy_data"].as<std::string>()));
}

nlohmann::json AmPolicyDataStore::merge_patch(const std::string& ue_id,
                                              const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT policy_data FROM udr_am_policy_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    auto doc = result.empty()
                   ? nlohmann::json::object()
                   : nlohmann::json::parse(result.front()["policy_data"].as<std::string>());
    doc.merge_patch(patch);
    txn.exec("INSERT INTO udr_am_policy_data (ue_id, policy_data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET policy_data = EXCLUDED.policy_data",
             pqxx::params{ue_id, doc.dump()});
    txn.commit();
    return doc;
}

SmsfContext3gppStore::SmsfContext3gppStore(const std::string& conninfo) : conn_(conninfo) {}

void SmsfContext3gppStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_smsf_3gpp_context (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> SmsfContext3gppStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_smsf_3gpp_context WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

bool SmsfContext3gppStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("DELETE FROM udr_smsf_3gpp_context WHERE ue_id = $1", pqxx::params{ue_id});
    txn.commit();
    return result.affected_rows() > 0;
}

SmsfNon3GppContextStore::SmsfNon3GppContextStore(const std::string& conninfo) : conn_(conninfo) {}

void SmsfNon3GppContextStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_smsf_non3gpp_context (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> SmsfNon3GppContextStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_smsf_non3gpp_context WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

bool SmsfNon3GppContextStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("DELETE FROM udr_smsf_non3gpp_context WHERE ue_id = $1", pqxx::params{ue_id});
    txn.commit();
    return result.affected_rows() > 0;
}

IpSmGwContextStore::IpSmGwContextStore(const std::string& conninfo) : conn_(conninfo) {}

void IpSmGwContextStore::put(const std::string& ue_id, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_ip_sm_gw_context (ue_id, context) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET context = EXCLUDED.context",
             pqxx::params{ue_id, context.dump()});
    txn.commit();
}

std::optional<nlohmann::json> IpSmGwContextStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT context FROM udr_ip_sm_gw_context WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["context"].as<std::string>()));
}

std::optional<nlohmann::json> IpSmGwContextStore::apply_patch(const std::string& ue_id,
                                                              const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT context FROM udr_ip_sm_gw_context WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    auto context = nlohmann::json::parse(result.front()["context"].as<std::string>());
    context = context.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("UPDATE udr_ip_sm_gw_context SET context = $2::jsonb WHERE ue_id = $1",
             pqxx::params{ue_id, context.dump()});
    txn.commit();
    return std::make_optional(context);
}

bool IpSmGwContextStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("DELETE FROM udr_ip_sm_gw_context WHERE ue_id = $1", pqxx::params{ue_id});
    txn.commit();
    return result.affected_rows() > 0;
}

MessageWaitingDataStore::MessageWaitingDataStore(const std::string& conninfo) : conn_(conninfo) {}

bool MessageWaitingDataStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row = txn.exec("INSERT INTO udr_mwd (ue_id, data) VALUES ($1, $2::jsonb) "
                              "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data "
                              "RETURNING (xmax = 0) AS inserted",
                              pqxx::params{ue_id, data.dump()})
                         .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> MessageWaitingDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_mwd WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

std::optional<nlohmann::json>
MessageWaitingDataStore::apply_patch(const std::string& ue_id, const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_mwd WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    auto data = nlohmann::json::parse(result.front()["data"].as<std::string>());
    data = data.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("UPDATE udr_mwd SET data = $2::jsonb WHERE ue_id = $1",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
    return std::make_optional(data);
}

bool MessageWaitingDataStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("DELETE FROM udr_mwd WHERE ue_id = $1", pqxx::params{ue_id});
    txn.commit();
    return result.affected_rows() > 0;
}

RoamingInformationStore::RoamingInformationStore(const std::string& conninfo) : conn_(conninfo) {}

bool RoamingInformationStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row =
        txn.exec("INSERT INTO udr_roaming_information (ue_id, data) VALUES ($1, $2::jsonb) "
                 "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data "
                 "RETURNING (xmax = 0) AS inserted",
                 pqxx::params{ue_id, data.dump()})
            .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> RoamingInformationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_roaming_information WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

PeiInfoStore::PeiInfoStore(const std::string& conninfo) : conn_(conninfo) {}

bool PeiInfoStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row = txn.exec("INSERT INTO udr_pei_info (ue_id, data) VALUES ($1, $2::jsonb) "
                              "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data "
                              "RETURNING (xmax = 0) AS inserted",
                              pqxx::params{ue_id, data.dump()})
                         .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> PeiInfoStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_pei_info WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

CoverageRestrictionDataStore::CoverageRestrictionDataStore(const std::string& conninfo)
    : conn_(conninfo) {}

void CoverageRestrictionDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_coverage_restriction_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> CoverageRestrictionDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_coverage_restriction_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

LcsPrivacyDataStore::LcsPrivacyDataStore(const std::string& conninfo) : conn_(conninfo) {}

void LcsPrivacyDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_lcs_privacy_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> LcsPrivacyDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_lcs_privacy_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

LcsSubscriptionDataStore::LcsSubscriptionDataStore(const std::string& conninfo) : conn_(conninfo) {}

void LcsSubscriptionDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_lcs_subscription_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> LcsSubscriptionDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_lcs_subscription_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

LcsMoDataStore::LcsMoDataStore(const std::string& conninfo) : conn_(conninfo) {}

void LcsMoDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_lcs_mo_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> LcsMoDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_lcs_mo_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

PpDataStore::PpDataStore(const std::string& conninfo) : conn_(conninfo) {}

std::optional<nlohmann::json> PpDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_pp_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

nlohmann::json PpDataStore::apply_patch(const std::string& ue_id, const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_pp_data WHERE ue_id = $1", pqxx::params{ue_id});
    auto doc = result.empty() ? nlohmann::json::object()
                              : nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc = doc.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("INSERT INTO udr_pp_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, doc.dump()});
    txn.commit();
    return doc;
}

PpProfileDataStore::PpProfileDataStore(const std::string& conninfo) : conn_(conninfo) {}

void PpProfileDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_pp_profile_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> PpProfileDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_pp_profile_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

PpDataEntryStore::PpDataEntryStore(const std::string& conninfo) : conn_(conninfo) {}

bool PpDataEntryStore::put(const std::string& ue_id,
                           const std::string& af_instance_id,
                           nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row = txn.exec("INSERT INTO udr_pp_data_entry (ue_id, af_instance_id, data) "
                              "VALUES ($1, $2, $3::jsonb) "
                              "ON CONFLICT (ue_id, af_instance_id) DO UPDATE SET data = "
                              "EXCLUDED.data "
                              "RETURNING (xmax = 0) AS inserted",
                              pqxx::params{ue_id, af_instance_id, data.dump()})
                         .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> PpDataEntryStore::get(const std::string& ue_id,
                                                    const std::string& af_instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_pp_data_entry "
                                 "WHERE ue_id = $1 AND af_instance_id = $2",
                                 pqxx::params{ue_id, af_instance_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

bool PpDataEntryStore::remove(const std::string& ue_id, const std::string& af_instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("DELETE FROM udr_pp_data_entry "
                                 "WHERE ue_id = $1 AND af_instance_id = $2",
                                 pqxx::params{ue_id, af_instance_id});
    txn.commit();
    return result.affected_rows() > 0;
}

std::vector<nlohmann::json> PpDataEntryStore::list_for_ue(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_pp_data_entry WHERE ue_id = $1", pqxx::params{ue_id});
    std::vector<nlohmann::json> out;
    out.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
        out.push_back(nlohmann::json::parse(row["data"].as<std::string>()));
    }
    return out;
}

SharedDataStore::SharedDataStore(const std::string& conninfo) : conn_(conninfo) {}

void SharedDataStore::seed(const std::string& shared_data_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_shared_data (shared_data_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (shared_data_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{shared_data_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> SharedDataStore::get(const std::string& shared_data_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_shared_data WHERE shared_data_id = $1",
                                 pqxx::params{shared_data_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

OperatorSpecificDataStore::OperatorSpecificDataStore(const std::string& conninfo)
    : conn_(conninfo) {}

std::optional<nlohmann::json> OperatorSpecificDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_operator_specific_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

nlohmann::json OperatorSpecificDataStore::apply_patch(const std::string& ue_id,
                                                      const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_operator_specific_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    auto doc = result.empty() ? nlohmann::json::object()
                              : nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc = doc.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("INSERT INTO udr_operator_specific_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, doc.dump()});
    txn.commit();
    return doc;
}

EeProfileDataStore::EeProfileDataStore(const std::string& conninfo) : conn_(conninfo) {}

void EeProfileDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_ee_profile_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> EeProfileDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_ee_profile_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

UePolicySetStore::UePolicySetStore(const std::string& conninfo) : conn_(conninfo) {}

bool UePolicySetStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row = txn.exec("INSERT INTO udr_ue_policy_set (ue_id, data) VALUES ($1, $2::jsonb) "
                              "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data "
                              "RETURNING (xmax = 0) AS inserted",
                              pqxx::params{ue_id, data.dump()})
                         .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> UePolicySetStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_ue_policy_set WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

nlohmann::json UePolicySetStore::merge_patch(const std::string& ue_id,
                                             const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_ue_policy_set WHERE ue_id = $1", pqxx::params{ue_id});
    auto doc = result.empty() ? nlohmann::json::object()
                              : nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc.merge_patch(patch);
    txn.exec("INSERT INTO udr_ue_policy_set (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, doc.dump()});
    txn.commit();
    return doc;
}

PolicyOperatorSpecificDataStore::PolicyOperatorSpecificDataStore(const std::string& conninfo)
    : conn_(conninfo) {}

std::optional<nlohmann::json> PolicyOperatorSpecificDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec(
        "SELECT data FROM udr_policy_operator_specific_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

nlohmann::json PolicyOperatorSpecificDataStore::apply_patch(const std::string& ue_id,
                                                            const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec(
        "SELECT data FROM udr_policy_operator_specific_data WHERE ue_id = $1", pqxx::params{ue_id});
    auto doc = result.empty() ? nlohmann::json::object()
                              : nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc = doc.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("INSERT INTO udr_policy_operator_specific_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, doc.dump()});
    txn.commit();
    return doc;
}

SponsorConnectivityDataStore::SponsorConnectivityDataStore(const std::string& conninfo)
    : conn_(conninfo) {}

void SponsorConnectivityDataStore::seed(const std::string& sponsor_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_sponsor_connectivity_data (sponsor_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (sponsor_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{sponsor_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> SponsorConnectivityDataStore::get(const std::string& sponsor_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_sponsor_connectivity_data WHERE sponsor_id = $1",
                 pqxx::params{sponsor_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

BdtDataStore::BdtDataStore(const std::string& conninfo) : conn_(conninfo) {}

void BdtDataStore::put(const std::string& bdt_ref_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_bdt_data (bdt_ref_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (bdt_ref_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{bdt_ref_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> BdtDataStore::get(const std::string& bdt_ref_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_bdt_data WHERE bdt_ref_id = $1", pqxx::params{bdt_ref_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

std::optional<nlohmann::json> BdtDataStore::merge_patch(const std::string& bdt_ref_id,
                                                        const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_bdt_data WHERE bdt_ref_id = $1", pqxx::params{bdt_ref_id});
    if (result.empty()) {
        // Real, disclosed: unlike AmPolicyDataStore/UePolicySetStore, this PATCH is NOT
        // upsert-capable -- the real spec documents a real 404 for UpdateIndividualBdtData when
        // the resource doesn't already exist (PUT is the real create path for this resource).
        return std::nullopt;
    }
    auto doc = nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc.merge_patch(patch);
    txn.exec("UPDATE udr_bdt_data SET data = $2::jsonb WHERE bdt_ref_id = $1",
             pqxx::params{bdt_ref_id, doc.dump()});
    txn.commit();
    return std::make_optional(doc);
}

bool BdtDataStore::remove(const std::string& bdt_ref_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("DELETE FROM udr_bdt_data WHERE bdt_ref_id = $1", pqxx::params{bdt_ref_id});
    txn.commit();
    return result.affected_rows() > 0;
}

PlmnUePolicySetStore::PlmnUePolicySetStore(const std::string& conninfo) : conn_(conninfo) {}

void PlmnUePolicySetStore::seed(const std::string& plmn_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_plmn_ue_policy_set (plmn_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (plmn_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{plmn_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> PlmnUePolicySetStore::get(const std::string& plmn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_plmn_ue_policy_set WHERE plmn_id = $1",
                                 pqxx::params{plmn_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

SlicePolicyDataStore::SlicePolicyDataStore(const std::string& conninfo) : conn_(conninfo) {}

std::optional<nlohmann::json> SlicePolicyDataStore::get(const std::string& snssai) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_slice_control_data WHERE snssai = $1", pqxx::params{snssai});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

nlohmann::json SlicePolicyDataStore::merge_patch(const std::string& snssai,
                                                 const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_slice_control_data WHERE snssai = $1", pqxx::params{snssai});
    auto doc = result.empty() ? nlohmann::json::object()
                              : nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc.merge_patch(patch);
    txn.exec("INSERT INTO udr_slice_control_data (snssai, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (snssai) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{snssai, doc.dump()});
    txn.commit();
    return doc;
}

GroupPolicyDataStore::GroupPolicyDataStore(const std::string& conninfo) : conn_(conninfo) {}

std::optional<nlohmann::json> GroupPolicyDataStore::get(const std::string& int_group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_group_control_data WHERE int_group_id = $1",
                                 pqxx::params{int_group_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

nlohmann::json GroupPolicyDataStore::merge_patch(const std::string& int_group_id,
                                                 const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_group_control_data WHERE int_group_id = $1",
                                 pqxx::params{int_group_id});
    auto doc = result.empty() ? nlohmann::json::object()
                              : nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc.merge_patch(patch);
    txn.exec("INSERT INTO udr_group_control_data (int_group_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (int_group_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{int_group_id, doc.dump()});
    txn.commit();
    return doc;
}

RoutingIdStore::RoutingIdStore(const std::string& conninfo) : conn_(conninfo) {}

void RoutingIdStore::seed(const std::string& nf_type,
                          const std::string& nf_group_id,
                          nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_routing_ids (nf_type, nf_group_id, data) "
             "VALUES ($1, $2, $3::jsonb) "
             "ON CONFLICT (nf_type, nf_group_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{nf_type, nf_group_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> RoutingIdStore::get(const std::string& nf_type,
                                                  const std::string& nf_group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_routing_ids WHERE nf_type = $1 AND nf_group_id = $2",
                 pqxx::params{nf_type, nf_group_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

NiddAuthorizationInfoStore::NiddAuthorizationInfoStore(const std::string& conninfo)
    : conn_(conninfo) {}

bool NiddAuthorizationInfoStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row =
        txn.exec("INSERT INTO udr_nidd_authorization_info (ue_id, data) VALUES ($1, $2::jsonb) "
                 "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data "
                 "RETURNING (xmax = 0) AS inserted",
                 pqxx::params{ue_id, data.dump()})
            .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json> NiddAuthorizationInfoStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_nidd_authorization_info WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

std::optional<nlohmann::json>
NiddAuthorizationInfoStore::apply_patch(const std::string& ue_id, const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_nidd_authorization_info WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    auto data = nlohmann::json::parse(result.front()["data"].as<std::string>());
    data = data.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("UPDATE udr_nidd_authorization_info SET data = $2::jsonb WHERE ue_id = $1",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
    return std::make_optional(data);
}

bool NiddAuthorizationInfoStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("DELETE FROM udr_nidd_authorization_info WHERE ue_id = $1", pqxx::params{ue_id});
    txn.commit();
    return result.affected_rows() > 0;
}

IdentityDataStore::IdentityDataStore(const std::string& conninfo) : conn_(conninfo) {}

std::optional<nlohmann::json> IdentityDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_identity_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

nlohmann::json IdentityDataStore::apply_patch(const std::string& ue_id,
                                              const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_identity_data WHERE ue_id = $1", pqxx::params{ue_id});
    auto doc = result.empty() ? nlohmann::json::object()
                              : nlohmann::json::parse(result.front()["data"].as<std::string>());
    doc = doc.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("INSERT INTO udr_identity_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, doc.dump()});
    txn.commit();
    return doc;
}

OdbDataStore::OdbDataStore(const std::string& conninfo) : conn_(conninfo) {}

void OdbDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_odb_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> OdbDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_odb_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

V2xDataStore::V2xDataStore(const std::string& conninfo) : conn_(conninfo) {}

void V2xDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_v2x_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> V2xDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_v2x_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

ProseDataStore::ProseDataStore(const std::string& conninfo) : conn_(conninfo) {}

void ProseDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_prose_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> ProseDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_prose_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

UcDataStore::UcDataStore(const std::string& conninfo) : conn_(conninfo) {}

void UcDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_uc_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> UcDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_uc_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

TimeSyncDataStore::TimeSyncDataStore(const std::string& conninfo) : conn_(conninfo) {}

void TimeSyncDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_time_sync_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> TimeSyncDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_time_sync_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

LocationDataStore::LocationDataStore(const std::string& conninfo) : conn_(conninfo) {}

void LocationDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_location_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> LocationDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_location_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

A2xDataStore::A2xDataStore(const std::string& conninfo) : conn_(conninfo) {}

void A2xDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_a2x_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> A2xDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_a2x_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

RangingSlPrivacyDataStore::RangingSlPrivacyDataStore(const std::string& conninfo)
    : conn_(conninfo) {}

void RangingSlPrivacyDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_rangingsl_privacy_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> RangingSlPrivacyDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_rangingsl_privacy_data WHERE ue_id = $1",
                                 pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

RangingSlPosDataStore::RangingSlPosDataStore(const std::string& conninfo) : conn_(conninfo) {}

void RangingSlPosDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_ranging_slpos_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> RangingSlPosDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_ranging_slpos_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

MbsDataStore::MbsDataStore(const std::string& conninfo) : conn_(conninfo) {}

void MbsDataStore::seed(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_5mbs_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> MbsDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_5mbs_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

ServiceSpecificAuthorizationInfoStore::ServiceSpecificAuthorizationInfoStore(
    const std::string& conninfo)
    : conn_(conninfo) {}

bool ServiceSpecificAuthorizationInfoStore::put(const std::string& ue_id,
                                                const std::string& service_type,
                                                nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto row =
        txn.exec("INSERT INTO udr_service_specific_auth_info (ue_id, service_type, data) "
                 "VALUES ($1, $2, $3::jsonb) "
                 "ON CONFLICT (ue_id, service_type) DO UPDATE SET data = EXCLUDED.data "
                 "RETURNING (xmax = 0) AS inserted",
                 pqxx::params{ue_id, service_type, data.dump()})
            .one_row();
    txn.commit();
    return row["inserted"].as<bool>();
}

std::optional<nlohmann::json>
ServiceSpecificAuthorizationInfoStore::get(const std::string& ue_id,
                                           const std::string& service_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_service_specific_auth_info "
                                 "WHERE ue_id = $1 AND service_type = $2",
                                 pqxx::params{ue_id, service_type});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

std::optional<nlohmann::json> ServiceSpecificAuthorizationInfoStore::apply_patch(
    const std::string& ue_id, const std::string& service_type, const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_service_specific_auth_info "
                                 "WHERE ue_id = $1 AND service_type = $2",
                                 pqxx::params{ue_id, service_type});
    if (result.empty()) {
        return std::nullopt;
    }
    auto data = nlohmann::json::parse(result.front()["data"].as<std::string>());
    data = data.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("UPDATE udr_service_specific_auth_info SET data = $3::jsonb "
             "WHERE ue_id = $1 AND service_type = $2",
             pqxx::params{ue_id, service_type, data.dump()});
    txn.commit();
    return std::make_optional(data);
}

bool ServiceSpecificAuthorizationInfoStore::remove(const std::string& ue_id,
                                                   const std::string& service_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("DELETE FROM udr_service_specific_auth_info "
                                 "WHERE ue_id = $1 AND service_type = $2",
                                 pqxx::params{ue_id, service_type});
    txn.commit();
    return result.affected_rows() > 0;
}

GroupIdentifiersStore::GroupIdentifiersStore(const std::string& conninfo) : conn_(conninfo) {}

void GroupIdentifiersStore::seed(const std::string& ext_group_id,
                                 const std::string& int_group_id,
                                 nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_group_identifiers (ext_group_id, int_group_id, data) "
             "VALUES ($1, $2, $3::jsonb) "
             "ON CONFLICT (ext_group_id) DO UPDATE SET int_group_id = EXCLUDED.int_group_id, "
             "data = EXCLUDED.data",
             pqxx::params{ext_group_id, int_group_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json>
GroupIdentifiersStore::get_by_ext_group_id(const std::string& ext_group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_group_identifiers WHERE ext_group_id = $1",
                                 pqxx::params{ext_group_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

std::optional<nlohmann::json>
GroupIdentifiersStore::get_by_int_group_id(const std::string& int_group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_group_identifiers WHERE int_group_id = $1",
                                 pqxx::params{int_group_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

NssaiAckDataStore::NssaiAckDataStore(const std::string& conninfo) : conn_(conninfo) {}

void NssaiAckDataStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_nssai_ack_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> NssaiAckDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_nssai_ack_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

CagAckDataStore::CagAckDataStore(const std::string& conninfo) : conn_(conninfo) {}

void CagAckDataStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_cag_ack_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> CagAckDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_cag_ack_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

SorDataStore::SorDataStore(const std::string& conninfo) : conn_(conninfo) {}

void SorDataStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_sor_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> SorDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_sor_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

std::optional<nlohmann::json> SorDataStore::apply_patch(const std::string& ue_id,
                                                        const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_sor_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    auto data = nlohmann::json::parse(result.front()["data"].as<std::string>());
    data = data.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("UPDATE udr_sor_data SET data = $2::jsonb WHERE ue_id = $1",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
    return std::make_optional(data);
}

UpuDataStore::UpuDataStore(const std::string& conninfo) : conn_(conninfo) {}

void UpuDataStore::put(const std::string& ue_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_upu_data (ue_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ue_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ue_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> UpuDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT data FROM udr_upu_data WHERE ue_id = $1", pqxx::params{ue_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

FiveGVnGroupStore::FiveGVnGroupStore(const std::string& conninfo) : conn_(conninfo) {}

void FiveGVnGroupStore::put(const std::string& ext_group_id, nlohmann::json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    txn.exec("INSERT INTO udr_5g_vn_groups (ext_group_id, data) VALUES ($1, $2::jsonb) "
             "ON CONFLICT (ext_group_id) DO UPDATE SET data = EXCLUDED.data",
             pqxx::params{ext_group_id, data.dump()});
    txn.commit();
}

std::optional<nlohmann::json> FiveGVnGroupStore::get(const std::string& ext_group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_5g_vn_groups WHERE ext_group_id = $1",
                                 pqxx::params{ext_group_id});
    if (result.empty()) {
        return std::nullopt;
    }
    return std::make_optional(nlohmann::json::parse(result.front()["data"].as<std::string>()));
}

std::optional<nlohmann::json> FiveGVnGroupStore::apply_patch(const std::string& ext_group_id,
                                                             const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT data FROM udr_5g_vn_groups WHERE ext_group_id = $1",
                                 pqxx::params{ext_group_id});
    if (result.empty()) {
        return std::nullopt;
    }
    auto data = nlohmann::json::parse(result.front()["data"].as<std::string>());
    data = data.patch(patch_ops); // may throw nlohmann::json::exception -- caller catches
    txn.exec("UPDATE udr_5g_vn_groups SET data = $2::jsonb WHERE ext_group_id = $1",
             pqxx::params{ext_group_id, data.dump()});
    txn.commit();
    return std::make_optional(data);
}

bool FiveGVnGroupStore::remove(const std::string& ext_group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("DELETE FROM udr_5g_vn_groups WHERE ext_group_id = $1",
                                 pqxx::params{ext_group_id});
    txn.commit();
    return result.affected_rows() > 0;
}

} // namespace udr
