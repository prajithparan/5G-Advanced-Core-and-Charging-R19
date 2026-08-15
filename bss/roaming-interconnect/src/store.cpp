#include "store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>

namespace roaming_interconnect {

RoamingCdrFile make_tap3_roaming_cdr_file(std::optional<std::string> agreementId,
                                          const tap3_core::DataInterchange& data) {
    RoamingCdrFile file;
    file.agreementId = std::move(agreementId);
    file.format = "TAP3";
    const auto bytes = tap3_core::encode_data_interchange(data);
    file.rawPayload.resize(bytes.size());
    std::transform(bytes.begin(), bytes.end(), file.rawPayload.begin(), [](std::uint8_t b) {
        return static_cast<std::byte>(b);
    });
    return file;
}

std::optional<tap3_core::DataInterchange> decode_tap3_roaming_cdr_file(const RoamingCdrFile& file) {
    if (file.format != "TAP3") {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(file.rawPayload.size());
    std::transform(file.rawPayload.begin(), file.rawPayload.end(), bytes.begin(), [](std::byte b) {
        return static_cast<std::uint8_t>(b);
    });
    return tap3_core::decode_data_interchange(bytes);
}

namespace {

using nlohmann::json;

// P4.5/ADR-0060 (E8, Security): real audit trail, same transaction as the mutation it records --
// see bss/product-catalog's own schema.sql header for the full "local per-service table" real
// architectural disclosure.
void write_audit_record(pqxx::work& txn,
                        const std::string& entity_type,
                        const std::string& entity_id,
                        const std::string& action,
                        const std::optional<std::string>& after_snapshot) {
    const auto id = txn.exec("SELECT nextval('audit_record_id_seq')::text AS id")
                        .one_row()["id"]
                        .as<std::string>();
    txn.exec("INSERT INTO audit_record (id, entity_type, entity_id, action, actor, "
             "after_snapshot) VALUES ($1,$2,$3,$4,'bss/roaming-interconnect',$5::jsonb)",
             pqxx::params{id, entity_type, entity_id, action, after_snapshot});
}

template <typename T> std::string dump_array(const std::vector<T>& v) {
    return json(v).dump();
}

template <typename T> std::vector<T> parse_array(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    return json::parse(s).get<std::vector<T>>();
}

template <typename T> std::optional<std::string> dump_optional(const std::optional<T>& v) {
    if (!v.has_value()) {
        return std::nullopt;
    }
    return json(*v).dump();
}

template <typename T> std::optional<T> parse_optional(const std::optional<std::string>& s) {
    if (!s.has_value()) {
        return std::nullopt;
    }
    return json::parse(*s).get<T>();
}

template <typename Row> InterconnectAgreement row_to_agreement(const Row& row) {
    InterconnectAgreement v;
    v.id = row["id"].template as<std::optional<std::string>>();
    v.href = row["href"].template as<std::optional<std::string>>();
    v.partnerOperatorPlmnId =
        row["partner_operator_plmn_id"].template as<std::optional<std::string>>();

    bss_sid::Agreement& a = v.agreement;
    a.id = v.id;
    a.href = v.href;
    a.agreementType = row["agreement_type"].template as<std::optional<std::string>>();
    a.description = row["description"].template as<std::optional<std::string>>();
    a.documentNumber = row["document_number"].template as<std::optional<int>>();
    a.initialDate = row["initial_date"].template as<std::optional<std::string>>();
    a.name = row["name"].template as<std::optional<std::string>>();
    a.statementOfIntent = row["statement_of_intent"].template as<std::optional<std::string>>();
    a.status = row["status"].template as<std::optional<std::string>>();
    a.version = row["version"].template as<std::optional<std::string>>();
    a.agreementAuthorization = parse_array<bss_sid::AgreementAuthorization>(
        row["agreement_authorization"].template as<std::string>());
    a.agreementItem =
        parse_array<bss_sid::AgreementItem>(row["agreement_item"].template as<std::string>());
    if (const auto from = row["agreement_period_from"].template as<std::optional<std::string>>(),
        to = row["agreement_period_to"].template as<std::optional<std::string>>();
        from.has_value() || to.has_value()) {
        a.agreementPeriod = bss_sid::TimePeriod{.startDateTime = from, .endDateTime = to};
    }
    a.agreementSpecification = parse_optional<bss_sid::AgreementSpecificationRef>(
        row["agreement_specification"].template as<std::optional<std::string>>());
    a.associatedAgreement =
        parse_array<bss_sid::AgreementRef>(row["associated_agreement"].template as<std::string>());
    a.characteristic =
        parse_array<bss_sid::Characteristic>(row["characteristic"].template as<std::string>());
    if (const auto from = row["completion_date_from"].template as<std::optional<std::string>>(),
        to = row["completion_date_to"].template as<std::optional<std::string>>();
        from.has_value() || to.has_value()) {
        a.completionDate = bss_sid::TimePeriod{.startDateTime = from, .endDateTime = to};
    }
    a.engagedParty =
        parse_array<bss_sid::RelatedParty>(row["engaged_party"].template as<std::string>());

    v.rateTerms = [&row] {
        const auto raw = row["rate_terms"].template as<std::optional<std::string>>();
        return raw.has_value() ? json::parse(*raw) : json(nullptr);
    }();
    return v;
}

} // namespace

InterconnectAgreementStore::InterconnectAgreementStore(std::string resource_url,
                                                       const std::string& conninfo)
    : resource_url_(std::move(resource_url)), conn_(conninfo) {}

std::string InterconnectAgreementStore::create(InterconnectAgreement agreement) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto id = txn.exec("SELECT nextval('interconnect_agreement_id_seq')::text AS id")
                        .one_row()["id"]
                        .as<std::string>();
    agreement.id = id;
    agreement.href = resource_url_ + "/" + id;
    const auto& a = agreement.agreement;

    txn.exec("INSERT INTO interconnect_agreement "
             "(id, href, partner_operator_plmn_id, agreement_type, description, document_number, "
             "initial_date, name, statement_of_intent, status, version, agreement_authorization, "
             "agreement_item, agreement_period_from, agreement_period_to, agreement_specification, "
             "associated_agreement, characteristic, completion_date_from, completion_date_to, "
             "engaged_party, rate_terms) "
             "VALUES "
             "($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12::jsonb,$13::jsonb,$14,$15,$16::jsonb,"
             "$17::jsonb,$18::jsonb,$19,$20,$21::jsonb,$22::jsonb)",
             pqxx::params{
                 id,
                 agreement.href,
                 agreement.partnerOperatorPlmnId,
                 a.agreementType,
                 a.description,
                 a.documentNumber,
                 a.initialDate,
                 a.name,
                 a.statementOfIntent,
                 a.status,
                 a.version,
                 dump_array(a.agreementAuthorization),
                 dump_array(a.agreementItem),
                 a.agreementPeriod.has_value() ? a.agreementPeriod->startDateTime : std::nullopt,
                 a.agreementPeriod.has_value() ? a.agreementPeriod->endDateTime : std::nullopt,
                 dump_optional(a.agreementSpecification),
                 dump_array(a.associatedAgreement),
                 dump_array(a.characteristic),
                 a.completionDate.has_value() ? a.completionDate->startDateTime : std::nullopt,
                 a.completionDate.has_value() ? a.completionDate->endDateTime : std::nullopt,
                 dump_array(a.engagedParty),
                 agreement.rateTerms.is_null()
                     ? std::optional<std::string>{std::nullopt}
                     : std::optional<std::string>{agreement.rateTerms.dump()}});

    write_audit_record(
        txn, "INTERCONNECT_AGREEMENT", id, "interconnectAgreement.create", json(a).dump());
    txn.commit();
    return id;
}

std::optional<InterconnectAgreement> InterconnectAgreementStore::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result =
        txn.exec("SELECT * FROM interconnect_agreement WHERE id = $1", pqxx::params{id});
    if (result.empty()) {
        return std::nullopt;
    }
    return row_to_agreement(result.front());
}

std::vector<InterconnectAgreement> InterconnectAgreementStore::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT * FROM interconnect_agreement ORDER BY id");
    std::vector<InterconnectAgreement> out;
    out.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
        out.push_back(row_to_agreement(row));
    }
    return out;
}

RoamingCdrFileStore::RoamingCdrFileStore(const std::string& conninfo) : conn_(conninfo) {}

std::string RoamingCdrFileStore::create(RoamingCdrFile file) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto id = txn.exec("SELECT nextval('roaming_cdr_file_id_seq')::text AS id")
                        .one_row()["id"]
                        .as<std::string>();
    file.id = id;

    const pqxx::bytes_view payload_view(file.rawPayload);
    txn.exec("INSERT INTO roaming_cdr_file (id, agreement_id, format, raw_payload) "
             "VALUES ($1,$2,$3,$4)",
             pqxx::params{id, file.agreementId, file.format, payload_view});

    write_audit_record(txn, "ROAMING_CDR_FILE", id, "roamingCdrFile.create", std::nullopt);
    txn.commit();
    return id;
}

std::optional<RoamingCdrFile> RoamingCdrFileStore::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(conn_);
    const auto result = txn.exec("SELECT * FROM roaming_cdr_file WHERE id = $1", pqxx::params{id});
    if (result.empty()) {
        return std::nullopt;
    }
    const auto& row = result.front();
    RoamingCdrFile v;
    v.id = row["id"].as<std::optional<std::string>>();
    v.agreementId = row["agreement_id"].as<std::optional<std::string>>();
    v.format = row["format"].as<std::string>();
    const auto payload = row["raw_payload"].as<std::optional<pqxx::bytes>>();
    if (payload.has_value()) {
        v.rawPayload = *payload;
    }
    return v;
}

// Real TMF651 Agreement fields come from bss_sid::Agreement's own already-declared to_json/
// from_json (libs/bss-sid) -- this composes that with the two project-internal fields
// (docs/DATA_MODEL.md's own E7 disclosure) on top, id/href taken from InterconnectAgreement's own
// (kept in sync with `agreement.id`/`agreement.href` at create() time, see store.cpp above).
void to_json(nlohmann::json& j, const InterconnectAgreement& v) {
    j = nlohmann::json(v.agreement);
    if (v.id.has_value()) {
        j["id"] = *v.id;
    }
    if (v.href.has_value()) {
        j["href"] = *v.href;
    }
    if (v.partnerOperatorPlmnId.has_value()) {
        j["partnerOperatorPlmnId"] = *v.partnerOperatorPlmnId;
    }
    if (!v.rateTerms.is_null()) {
        j["rateTerms"] = v.rateTerms;
    }
}

void from_json(const nlohmann::json& j, InterconnectAgreement& v) {
    v.agreement = j.template get<bss_sid::Agreement>();
    if (const auto it = j.find("id"); it != j.end() && !it->is_null()) {
        v.id = it->get<std::string>();
    }
    if (const auto it = j.find("href"); it != j.end() && !it->is_null()) {
        v.href = it->get<std::string>();
    }
    if (const auto it = j.find("partnerOperatorPlmnId"); it != j.end() && !it->is_null()) {
        v.partnerOperatorPlmnId = it->get<std::string>();
    }
    if (const auto it = j.find("rateTerms"); it != j.end()) {
        v.rateTerms = *it;
    }
}

} // namespace roaming_interconnect
