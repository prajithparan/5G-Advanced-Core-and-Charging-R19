#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <mutex>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

#include "bss_sid/agreement.hpp"

// Private to bss/roaming-interconnect. Real PostgreSQL persistence (libpqxx), same "one shared
// connection, one mutex" discipline this project already applied to every other bss/* store --
// see schema.sql's own header for the full scoping disclosure (schema + store library only this
// turn, no new HTTP service yet -- CHARGING_PROMPT.md's P4.11 owns that).

namespace roaming_interconnect {

// E7 InterconnectAgreement -- project-internal, realized as a real TMF651 Agreement
// (bss_sid::Agreement) plus the two project-internal fields (partnerOperatorPlmnId, rateTerms)
// docs/DATA_MODEL.md's own sketch names, neither of which has a real TMF651 field home.
struct InterconnectAgreement {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> partnerOperatorPlmnId;
    bss_sid::Agreement agreement;
    nlohmann::json rateTerms; // opaque -- "partner-specific rating; shape TBD, not guessed"
};

void to_json(nlohmann::json& j, const InterconnectAgreement& v);
void from_json(const nlohmann::json& j, InterconnectAgreement& v);

// E7 RoamingCdrFile -- format is real-spec-disclosed as STUB until a real GSMA TAP3/RAP/NRTRDE
// spec is supplied (schema.sql's own header).
struct RoamingCdrFile {
    std::optional<std::string> id;
    std::optional<std::string> agreementId;
    std::string format = "STUB";       // TAP3 | RAP | NRTRDE | STUB
    std::vector<std::byte> rawPayload; // matches libpqxx's own real bytea representation
                                       // (pqxx::bytes = std::vector<std::byte>) directly, no cast
};

class InterconnectAgreementStore {
public:
    InterconnectAgreementStore(std::string resource_url, const std::string& conninfo);

    std::string create(InterconnectAgreement agreement);
    std::optional<InterconnectAgreement> get(const std::string& id);
    std::vector<InterconnectAgreement> list();

private:
    std::string resource_url_;
    std::mutex mutex_;
    pqxx::connection conn_;
};

class RoamingCdrFileStore {
public:
    explicit RoamingCdrFileStore(const std::string& conninfo);

    std::string create(RoamingCdrFile file);
    std::optional<RoamingCdrFile> get(const std::string& id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

} // namespace roaming_interconnect
