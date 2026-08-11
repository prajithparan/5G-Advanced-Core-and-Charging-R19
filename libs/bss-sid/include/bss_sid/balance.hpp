#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "bss_sid/product.hpp"

// TM Forum SID Balance -> TMF654 Prepay Balance Management. Hand-written, not codegen'd -- same
// "no TMF spec vendored, hand-roll it against a real confirmed source" precedent as
// product.hpp/party.hpp (this file's siblings). Fields confirmed by downloading and directly
// parsing the real TMF654 v4.0.0 swagger JSON
// (github.com/tmforum-apis/TMF654_PrepayBalanceManagement,
// TMF654-PrepayBalance-v4.0.0.swagger.json), not recalled from memory -- real basePath
// `/tmf-api/prepayBalanceManagement/v4`.
//
// TimePeriod/Money/Quantity/ChannelRef are already modeled in product.hpp with an identical real
// shape (confirmed independently against both TMF620's and TMF654's own swagger -- these are TM
// Forum's shared common types, not TMF620-specific), reused directly rather than redefined here.
//
// Scope, matching CHARGING_PROMPT.md's P4.3 (Rating Engine + ABMF) real requirements ("unit
// reservation and refund, multi-balance... explicit currency and rounding rules"): `Bucket` (the
// balance resource itself), `TopupBalance` (real credit/funding path -- the real TMF654 API has
// NO `POST /bucket` at all, confirmed directly against the swagger; a bucket only comes into
// being via a topup, per this project's own disclosed interpretation, see bss/balance-management's
// own README/main.cpp comment), `AdjustBalance` (real signed debit/credit),
// `ReserveBalance` (real reserve/unreserve -- sign-of-amount interpretation disclosed, not
// confirmed from spec prose, see main.cpp), `AccumulatedBalance` (real aggregation query).
// Deliberately NOT modeled this pass: `TransferBalance`, `BalanceActionHistory` (real resources,
// just not needed to prove P4.3's core ask -- deterministic rating debiting a real, strongly-
// consistent balance under concurrency).
//
// Real, important correction recorded here (see docs/DATA_MODEL.md's own E6 section for the
// fuller story): `Bucket.usageType`'s real enum is `{monetary, voice, data, sms, other}` -- what
// KIND of quantity a bucket tracks, not a MAIN/BONUS/PROMOTIONAL "pool" distinction (TMF654 has no
// such enum at all; multi-balance is modeled as separate Bucket resources, distinguished by
// name/description, not a fixed field).

namespace bss_sid {

// Real TMF654 Ref shapes -- id/href/name(/description/status), each confirmed individually
// against the real swagger's `definitions`.
struct PartyAccountRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> description;
    std::optional<std::string> name;
    std::optional<std::string> status;
};
void to_json(nlohmann::json& j, const PartyAccountRef& v);
void from_json(const nlohmann::json& j, PartyAccountRef& v);

struct ProductRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const ProductRef& v);
void from_json(const nlohmann::json& j, ProductRef& v);

struct LogicalResourceRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const LogicalResourceRef& v);
void from_json(const nlohmann::json& j, LogicalResourceRef& v);

struct BucketRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const BucketRef& v);
void from_json(const nlohmann::json& j, BucketRef& v);

struct RelatedParty {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> role;
};
void to_json(nlohmann::json& j, const RelatedParty& v);
void from_json(const nlohmann::json& j, RelatedParty& v);

// Real TMF654 Bucket -- the balance resource itself. Real confirmed fields: id, href,
// confirmationDate, description, isShared, name, remainingValueName, requestedDate,
// logicalResource, partyAccount, product, relatedParty, remainingValue, reservedValue, status,
// usageType, validFor. `isShared` is the real TMF654 field this project's E10 (enterprise pooled
// quota) requirement maps onto directly (docs/DATA_MODEL.md).
struct Bucket {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> confirmationDate;
    std::optional<std::string> description;
    std::optional<bool> isShared;
    std::optional<std::string> name;
    std::optional<std::string> remainingValueName;
    std::optional<std::string> requestedDate;
    std::optional<LogicalResourceRef> logicalResource;
    std::optional<PartyAccountRef> partyAccount;
    std::optional<ProductRef> product;
    std::vector<RelatedParty> relatedParty;
    std::optional<Money> remainingValue;
    std::optional<Money> reservedValue;
    std::optional<std::string> status;    // BucketStatusType: active/suspended/expired
    std::optional<std::string> usageType; // UsageType: monetary/voice/data/sms/other
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const Bucket& v);
void from_json(const nlohmann::json& j, Bucket& v);

// Real TMF654 AccumulatedBalance -- aggregates a party account's buckets into a totalBalance.
// Real confirmed fields: id, href, description, name, bucket, logicalResource, partyAccount,
// product, relatedParty, totalBalance.
struct AccumulatedBalance {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> description;
    std::optional<std::string> name;
    std::vector<BucketRef> bucket;
    std::optional<LogicalResourceRef> logicalResource;
    std::optional<PartyAccountRef> partyAccount;
    std::optional<ProductRef> product;
    std::vector<RelatedParty> relatedParty;
    std::optional<Money> totalBalance;
};
void to_json(nlohmann::json& j, const AccumulatedBalance& v);
void from_json(const nlohmann::json& j, AccumulatedBalance& v);

// Real TMF654 TopupBalance -- real confirmed fields: id, href, confirmationDate, description,
// isAutoTopup, numberOfPeriods, reason, requestedDate, voucher, amount, balanceTopup, bucket,
// channel, logicalResource, partyAccount, paymentMethod, product, recurringPeriod, relatedParty,
// requestor, status, usageType, validFor. Only the fields this project's real use needs are
// modeled (amount, bucket, partyAccount, product, status, usageType, description, reason) --
// `isAutoTopup`/`numberOfPeriods`/`voucher`/`balanceTopup`/`channel`/`paymentMethod`/
// `recurringPeriod`/`relatedParty`/`requestor`/`validFor` are real fields, not modeled yet, per
// CLAUDE.md's "no speculative abstraction" rule.
struct TopupBalance {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> confirmationDate;
    std::optional<std::string> description;
    std::optional<std::string> reason;
    std::optional<std::string> requestedDate;
    std::optional<Quantity> amount;
    std::optional<BucketRef> bucket;
    std::optional<PartyAccountRef> partyAccount;
    std::optional<ProductRef> product;
    std::optional<std::string> status; // ActionStatusType: created/failed/cancelled/completed
    std::optional<std::string> usageType;
};
void to_json(nlohmann::json& j, const TopupBalance& v);
void from_json(const nlohmann::json& j, TopupBalance& v);

// Real TMF654 AdjustBalance -- real confirmed fields: id, href, confirmationDate, description,
// reason, requestedDate, adjustType, amount, bucket, channel, logicalResource, partyAccount,
// product, relatedParty, requestor, status, usageType, validFor. Same "only what's needed" scope
// as TopupBalance above.
struct AdjustBalance {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> confirmationDate;
    std::optional<std::string> description;
    std::optional<std::string> reason;
    std::optional<std::string> requestedDate;
    std::optional<std::string> adjustType; // AdjustType: recurring/oneTime
    std::optional<Quantity> amount;
    std::optional<BucketRef> bucket;
    std::optional<PartyAccountRef> partyAccount;
    std::optional<ProductRef> product;
    std::optional<std::string> status;
    std::optional<std::string> usageType;
};
void to_json(nlohmann::json& j, const AdjustBalance& v);
void from_json(const nlohmann::json& j, AdjustBalance& v);

// Real TMF654 ReserveBalance -- real confirmed fields: id, href, confirmationDate, description,
// reason, requestedDate, amount, bucket, channel, logicalResource, partyAccount, product,
// relatedParty, requestor, status, usageType, validFor.
struct ReserveBalance {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> confirmationDate;
    std::optional<std::string> description;
    std::optional<std::string> reason;
    std::optional<std::string> requestedDate;
    std::optional<Quantity> amount;
    std::optional<BucketRef> bucket;
    std::optional<PartyAccountRef> partyAccount;
    std::optional<ProductRef> product;
    std::optional<std::string> status;
    std::optional<std::string> usageType;
};
void to_json(nlohmann::json& j, const ReserveBalance& v);
void from_json(const nlohmann::json& j, ReserveBalance& v);

} // namespace bss_sid
