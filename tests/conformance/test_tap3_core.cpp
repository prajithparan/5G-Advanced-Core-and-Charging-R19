// Unit tests for libs/tap3-core -- real round-trip encode/decode against real, cited field names
// and [APPLICATION N] tag numbers (TAP-SPEC.pdf, GSMA TD.57 "TAP 3.12 Format Specification"
// V36.4). See tap3_core/tap3_common.hpp's own header for the full real sourcing/scope disclosure
// -- GSMA member-confidential source, no verbatim excerpts, real cited facts only, same discipline
// this project already uses for TCAP/MAP/CAP. No genuine external TAP3 sample file exists to
// cross-check against without violating that same confidentiality boundary -- verification here is
// internal round-trip correctness plus real per-field tag-number citations already recorded in the
// production code these tests exercise (ADR-0067).

#include "tap3_core/tap3_aggregated_usage.hpp"
#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_content_transaction.hpp"
#include "tap3_core/tap3_envelope.hpp"
#include "tap3_core/tap3_gprs_call.hpp"
#include "tap3_core/tap3_location_service.hpp"
#include "tap3_core/tap3_messaging_event.hpp"
#include "tap3_core/tap3_mo_call.hpp"
#include "tap3_core/tap3_mobile_session.hpp"
#include "tap3_core/tap3_mt_call.hpp"
#include "tap3_core/tap3_scu.hpp"
#include "tap3_core/tap3_suppl_service.hpp"

#include <gtest/gtest.h>

using namespace tap3_core;

TEST(Tap3Common, DateTimeLongRoundTrips) {
    DateTimeLong dtl;
    dtl.localTimeStamp = "20260815120000"; // real CCYYMMDDhhmmss format
    dtl.utcTimeOffset = "+0100";

    const auto tlv = encode_date_time_long(Tag::kFileCreationTimeStamp, dtl);
    EXPECT_EQ(tlv.tag_class, TagClass::kApplication);
    EXPECT_EQ(tlv.tag_number, Tag::kFileCreationTimeStamp);

    const auto decoded = decode_date_time_long(tlv, Tag::kFileCreationTimeStamp);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded->localTimeStamp, "20260815120000");
    EXPECT_EQ(*decoded->utcTimeOffset, "+0100");
}

TEST(Tap3Common, DateTimeRoundTripsWithOffsetCode) {
    DateTime dt;
    dt.localTimeStamp = "20260815120000";
    dt.utcTimeOffsetCode = 3;

    const auto tlv = encode_date_time(MoCallTag::kCallEventStartTimeStamp, dt);
    const auto decoded = decode_date_time(tlv, MoCallTag::kCallEventStartTimeStamp);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded->localTimeStamp, "20260815120000");
    EXPECT_EQ(*decoded->utcTimeOffsetCode, 3);
}

TEST(Tap3Envelope, BatchControlInfoRoundTrips) {
    BatchControlInfo bci;
    bci.sender = "OPERA"; // real Sender ::= PlmnId ::= AsciiString(SIZE(5))
    bci.recipient = "OPERB";
    bci.fileSequenceNumber = "00001"; // real NumberString(SIZE(5))
    bci.specificationVersionNumber = 3;
    bci.releaseVersionNumber = 12;
    bci.fileTypeIndicator = "P"; // production, per TAP-SPEC.pdf's own real code list
    bci.operatorSpecInformation = {"note-a", "note-b"};

    const auto tlv = encode_batch_control_info(bci);
    EXPECT_EQ(tlv.tag_number, Tag::kBatchControlInfo);

    const auto decoded = decode_batch_control_info(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded->sender, "OPERA");
    EXPECT_EQ(*decoded->recipient, "OPERB");
    EXPECT_EQ(*decoded->fileSequenceNumber, "00001");
    EXPECT_EQ(*decoded->specificationVersionNumber, 3);
    EXPECT_EQ(*decoded->releaseVersionNumber, 12);
    EXPECT_EQ(*decoded->fileTypeIndicator, "P");
    ASSERT_EQ(decoded->operatorSpecInformation.size(), 2u);
    EXPECT_EQ(decoded->operatorSpecInformation[0], "note-a");
    EXPECT_EQ(decoded->operatorSpecInformation[1], "note-b");
}

TEST(Tap3Envelope, AccountingInfoWithTaxationAndDiscountingRoundTrips) {
    AccountingInfo ai;
    Taxation tax;
    tax.taxCode = 1;
    tax.taxType = "VA";      // VAT, real 2-char code
    tax.taxRate = "0170000"; // real fixed-length-7 NumberString, e.g. 17.0000%
    ai.taxation.push_back(tax);

    Discounting disc;
    disc.discountCode = 5;
    DiscountApplied applied;
    applied.isFixedValue = false;
    applied.value = 1500; // real PercentageRate encoding, 15.00%
    disc.discountApplied = applied;
    ai.discounting.push_back(disc);

    ai.localCurrency = "EUR";
    ai.tapCurrency = "USD";
    ai.tapDecimalPlaces = 3;

    const auto tlv = encode_accounting_info(ai);
    const auto decoded = decode_accounting_info(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->taxation.size(), 1u);
    EXPECT_EQ(*decoded->taxation[0].taxCode, 1);
    EXPECT_EQ(*decoded->taxation[0].taxType, "VA");
    EXPECT_EQ(*decoded->taxation[0].taxRate, "0170000");
    ASSERT_EQ(decoded->discounting.size(), 1u);
    EXPECT_EQ(*decoded->discounting[0].discountCode, 5);
    ASSERT_TRUE(decoded->discounting[0].discountApplied.has_value());
    EXPECT_FALSE(decoded->discounting[0].discountApplied->isFixedValue);
    EXPECT_EQ(decoded->discounting[0].discountApplied->value, 1500);
    EXPECT_EQ(*decoded->localCurrency, "EUR");
    EXPECT_EQ(*decoded->tapCurrency, "USD");
    EXPECT_EQ(*decoded->tapDecimalPlaces, 3);
}

TEST(Tap3Envelope, DiscountAppliedFixedValueBranchRoundTrips) {
    DiscountApplied applied;
    applied.isFixedValue = true;
    applied.value = 250;

    const auto tlv = encode_discount_applied(applied);
    EXPECT_EQ(tlv.tag_class, TagClass::kApplication);
    EXPECT_EQ(tlv.tag_number, Tag::kDiscountApplied);
    EXPECT_TRUE(tlv.constructed); // real EXPLICIT wrap -- CHOICE tagged even under IMPLICIT TAGS

    const auto decoded = decode_discount_applied(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->isFixedValue);
    EXPECT_EQ(decoded->value, 250);
}

TEST(Tap3Envelope, NetworkInfoRoundTrips) {
    NetworkInfo ni;
    UtcTimeOffsetInfo offset;
    offset.utcTimeOffsetCode = 1;
    offset.utcTimeOffset = "+0000";
    ni.utcTimeOffsetInfo.push_back(offset);

    RecEntityInformation rec;
    rec.recEntityCode = 7;
    rec.recEntityType = 1; // MSC, per TAP-SPEC.pdf's own real code list
    rec.recEntityId = "MSC01";
    ni.recEntityInfo.push_back(rec);

    const auto tlv = encode_network_info(ni);
    const auto decoded = decode_network_info(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->utcTimeOffsetInfo.size(), 1u);
    EXPECT_EQ(*decoded->utcTimeOffsetInfo[0].utcTimeOffsetCode, 1);
    EXPECT_EQ(*decoded->utcTimeOffsetInfo[0].utcTimeOffset, "+0000");
    ASSERT_EQ(decoded->recEntityInfo.size(), 1u);
    EXPECT_EQ(*decoded->recEntityInfo[0].recEntityCode, 7);
    EXPECT_EQ(*decoded->recEntityInfo[0].recEntityId, "MSC01");
}

TEST(Tap3Envelope, AuditControlInfoRoundTrips) {
    AuditControlInfo aci;
    aci.totalCharge = 123456;
    aci.totalTaxValue = 4321;
    aci.totalDiscountValue = 100;
    aci.callEventDetailsCount = 1;

    const auto tlv = encode_audit_control_info(aci);
    const auto decoded = decode_audit_control_info(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded->totalCharge, 123456);
    EXPECT_EQ(*decoded->totalTaxValue, 4321);
    EXPECT_EQ(*decoded->totalDiscountValue, 100);
    EXPECT_EQ(*decoded->callEventDetailsCount, 1);
}

TEST(Tap3MoCall, ChargeableSubscriberSimBranchRoundTrips) {
    ChargeableSubscriber sub;
    sub.isSim = true;
    sub.imsi = "999700000000001"; // real BCDString content modeled as digit string here
    sub.msisdn = "447700900001";

    const auto tlv = encode_chargeable_subscriber(sub);
    EXPECT_EQ(tlv.tag_number, MoCallTag::kChargeableSubscriber);
    EXPECT_TRUE(tlv.constructed); // real EXPLICIT wrap

    const auto decoded = decode_chargeable_subscriber(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->isSim);
    EXPECT_EQ(*decoded->imsi, "999700000000001");
    EXPECT_EQ(*decoded->msisdn, "447700900001");
}

TEST(Tap3MoCall, ChargeableSubscriberMinBranchRoundTrips) {
    ChargeableSubscriber sub;
    sub.isSim = false;
    sub.min = "5551234567";
    sub.mdn = "5559876543";

    const auto tlv = encode_chargeable_subscriber(sub);
    const auto decoded = decode_chargeable_subscriber(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->isSim);
    EXPECT_EQ(*decoded->min, "5551234567");
    EXPECT_EQ(*decoded->mdn, "5559876543");
}

TEST(Tap3MoCall, ImeiOrEsnBothBranchesRoundTrip) {
    ImeiOrEsn imei;
    imei.isImei = true;
    imei.value = "35209900176148";
    const auto imei_tlv = encode_imei_or_esn(imei);
    const auto imei_decoded = decode_imei_or_esn(imei_tlv);
    ASSERT_TRUE(imei_decoded.has_value());
    EXPECT_TRUE(imei_decoded->isImei);
    EXPECT_EQ(imei_decoded->value, "35209900176148");

    ImeiOrEsn esn;
    esn.isImei = false;
    esn.value = "1234567890";
    const auto esn_tlv = encode_imei_or_esn(esn);
    const auto esn_decoded = decode_imei_or_esn(esn_tlv);
    ASSERT_TRUE(esn_decoded.has_value());
    EXPECT_FALSE(esn_decoded->isImei);
    EXPECT_EQ(esn_decoded->value, "1234567890");
}

TEST(Tap3MoCall, BasicServiceCodeTeleAndBearerBranchesRoundTrip) {
    BasicServiceCode tele;
    tele.isTeleService = true;
    tele.value = "11"; // real HexString(SIZE(2))
    const auto tele_decoded = decode_basic_service_code(encode_basic_service_code(tele));
    ASSERT_TRUE(tele_decoded.has_value());
    EXPECT_TRUE(tele_decoded->isTeleService);
    EXPECT_EQ(tele_decoded->value, "11");

    BasicServiceCode bearer;
    bearer.isTeleService = false;
    bearer.value = "42";
    const auto bearer_decoded = decode_basic_service_code(encode_basic_service_code(bearer));
    ASSERT_TRUE(bearer_decoded.has_value());
    EXPECT_FALSE(bearer_decoded->isTeleService);
    EXPECT_EQ(bearer_decoded->value, "42");
}

TEST(Tap3MoCall, LocationInformationRoundTrips) {
    LocationInformation loc;
    NetworkLocation nl;
    nl.recEntityCode = 3;
    nl.callReference = std::vector<std::uint8_t>{0x01, 0x02, 0x03};
    nl.locationArea = 100;
    nl.cellId = 55;
    loc.networkLocation = nl;

    HomeLocationInformation hl;
    hl.homeBid = "HOME1";
    hl.homeLocationDescription = "Home Country";
    loc.homeLocationInformation = hl;

    const auto tlv = encode_location_information(loc);
    const auto decoded = decode_location_information(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->networkLocation.has_value());
    EXPECT_EQ(*decoded->networkLocation->recEntityCode, 3);
    ASSERT_TRUE(decoded->networkLocation->callReference.has_value());
    EXPECT_EQ(*decoded->networkLocation->callReference,
              (std::vector<std::uint8_t>{0x01, 0x02, 0x03}));
    EXPECT_EQ(*decoded->networkLocation->locationArea, 100);
    EXPECT_EQ(*decoded->networkLocation->cellId, 55);
    ASSERT_TRUE(decoded->homeLocationInformation.has_value());
    EXPECT_EQ(*decoded->homeLocationInformation->homeBid, "HOME1");
}

TEST(Tap3MoCall, FullMobileOriginatedCallRoundTrips) {
    MobileOriginatedCall call;

    MoBasicCallInformation basic;
    ChargeableSubscriber sub;
    sub.isSim = true;
    sub.imsi = "999700000000001";
    basic.chargeableSubscriber = sub;
    Destination dest;
    dest.calledNumber = "447700900002";
    basic.destination = dest;
    basic.destinationNetwork = "44770";
    DateTime start;
    start.localTimeStamp = "20260815120000";
    basic.callEventStartTimeStamp = start;
    basic.totalCallEventDuration = 120;
    basic.causeForTerm = 0;
    call.basicCallInformation = basic;

    ImeiOrEsn imei;
    imei.isImei = true;
    imei.value = "35209900176148";
    call.equipmentIdentifier = imei;

    BasicServiceUsed bsu;
    BasicService bs;
    BasicServiceCode code;
    code.isTeleService = true;
    code.value = "11";
    bs.serviceCode = code;
    bsu.basicService = bs;
    call.basicServiceUsedList.push_back(bsu);

    call.supplServiceCode = "1F";

    ThirdPartyInformation third;
    third.thirdPartyNumber = "447700900003";
    call.thirdPartyInformation = third;

    CamelServiceUsed camel;
    camel.camelServiceLevel = 2;
    camel.camelServiceKey = 17;
    call.camelServiceUsed = camel;

    call.operatorSpecInformation = {"opnote"};

    const auto tlv = encode_mobile_originated_call(call);
    EXPECT_EQ(tlv.tag_class, TagClass::kApplication);
    EXPECT_EQ(tlv.tag_number, Tag::kMobileOriginatedCall); // real [APPLICATION 9]

    const auto decoded = decode_mobile_originated_call(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->basicCallInformation.has_value());
    ASSERT_TRUE(decoded->basicCallInformation->chargeableSubscriber.has_value());
    EXPECT_EQ(*decoded->basicCallInformation->chargeableSubscriber->imsi, "999700000000001");
    ASSERT_TRUE(decoded->basicCallInformation->destination.has_value());
    EXPECT_EQ(*decoded->basicCallInformation->destination->calledNumber, "447700900002");
    EXPECT_EQ(*decoded->basicCallInformation->totalCallEventDuration, 120);
    ASSERT_TRUE(decoded->equipmentIdentifier.has_value());
    EXPECT_EQ(decoded->equipmentIdentifier->value, "35209900176148");
    ASSERT_EQ(decoded->basicServiceUsedList.size(), 1u);
    ASSERT_TRUE(decoded->basicServiceUsedList[0].basicService.has_value());
    ASSERT_TRUE(decoded->basicServiceUsedList[0].basicService->serviceCode.has_value());
    EXPECT_EQ(decoded->basicServiceUsedList[0].basicService->serviceCode->value, "11");
    EXPECT_EQ(*decoded->supplServiceCode, "1F");
    ASSERT_TRUE(decoded->thirdPartyInformation.has_value());
    EXPECT_EQ(*decoded->thirdPartyInformation->thirdPartyNumber, "447700900003");
    ASSERT_TRUE(decoded->camelServiceUsed.has_value());
    EXPECT_EQ(*decoded->camelServiceUsed->camelServiceLevel, 2);
    ASSERT_EQ(decoded->operatorSpecInformation.size(), 1u);
    EXPECT_EQ(decoded->operatorSpecInformation[0], "opnote");
}

TEST(Tap3Envelope, TransferBatchWithMobileOriginatedCallRoundTripsThroughDataInterchange) {
    TransferBatch batch;

    BatchControlInfo bci;
    bci.sender = "OPERA";
    bci.recipient = "OPERB";
    bci.fileSequenceNumber = "00001";
    batch.batchControlInfo = bci;

    NetworkInfo ni;
    RecEntityInformation rec;
    rec.recEntityCode = 1;
    rec.recEntityId = "MSC01";
    ni.recEntityInfo.push_back(rec);
    batch.networkInfo = ni;

    MobileOriginatedCall call;
    ChargeableSubscriber sub;
    sub.isSim = true;
    sub.imsi = "999700000000001";
    MoBasicCallInformation basic;
    basic.chargeableSubscriber = sub;
    call.basicCallInformation = basic;

    CallEventDetailList details;
    details.mobileOriginatedCall.push_back(encode_mobile_originated_call(call));
    batch.callEventDetails = details;

    AuditControlInfo aci;
    aci.callEventDetailsCount = 1;
    batch.auditControlInfo = aci;

    DataInterchange di;
    di.transferBatch = batch;

    const auto bytes = encode_data_interchange(di);
    ASSERT_FALSE(bytes.empty());

    const auto decoded = decode_data_interchange(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->transferBatch.has_value());
    ASSERT_TRUE(decoded->transferBatch->batchControlInfo.has_value());
    EXPECT_EQ(*decoded->transferBatch->batchControlInfo->sender, "OPERA");
    ASSERT_TRUE(decoded->transferBatch->networkInfo.has_value());
    ASSERT_EQ(decoded->transferBatch->networkInfo->recEntityInfo.size(), 1u);
    ASSERT_TRUE(decoded->transferBatch->callEventDetails.has_value());
    ASSERT_EQ(decoded->transferBatch->callEventDetails->mobileOriginatedCall.size(), 1u);
    const auto decoded_call = decode_mobile_originated_call(
        decoded->transferBatch->callEventDetails->mobileOriginatedCall[0]);
    ASSERT_TRUE(decoded_call.has_value());
    ASSERT_TRUE(decoded_call->basicCallInformation.has_value());
    ASSERT_TRUE(decoded_call->basicCallInformation->chargeableSubscriber.has_value());
    EXPECT_EQ(*decoded_call->basicCallInformation->chargeableSubscriber->imsi, "999700000000001");
    ASSERT_TRUE(decoded->transferBatch->auditControlInfo.has_value());
    EXPECT_EQ(*decoded->transferBatch->auditControlInfo->callEventDetailsCount, 1);
}

TEST(Tap3Envelope, NotificationRoundTripsThroughDataInterchange) {
    Notification notif;
    notif.sender = "OPERA";
    notif.recipient = "OPERB";
    notif.fileSequenceNumber = "00002";
    notif.fileTypeIndicator = "T"; // real "test" indicator per the same real code list

    DataInterchange di;
    di.notification = notif;

    const auto bytes = encode_data_interchange(di);
    const auto decoded = decode_data_interchange(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->notification.has_value());
    EXPECT_EQ(*decoded->notification->sender, "OPERA");
    EXPECT_EQ(*decoded->notification->fileSequenceNumber, "00002");
    EXPECT_EQ(*decoded->notification->fileTypeIndicator, "T");
    EXPECT_FALSE(decoded->transferBatch.has_value());
}

TEST(Tap3MtCall, FullMobileTerminatedCallRoundTrips) {
    MobileTerminatedCall call;
    MtBasicCallInformation basic;
    ChargeableSubscriber sub;
    sub.isSim = true;
    sub.imsi = "999700000000002";
    basic.chargeableSubscriber = sub;
    CallOriginator orig;
    orig.callingNumber = "447700900010";
    orig.clirIndicator = 1;
    basic.callOriginator = orig;
    basic.originatingNetwork = "44770";
    DateTime start;
    start.localTimeStamp = "20260815120500";
    basic.callEventStartTimeStamp = start;
    basic.totalCallEventDuration = 60;
    call.basicCallInformation = basic;

    ImeiOrEsn imei;
    imei.isImei = true;
    imei.value = "35209900176149";
    call.equipmentIdentifier = imei;

    const auto tlv = encode_mobile_terminated_call(call);
    EXPECT_EQ(tlv.tag_number, Tag::kMobileTerminatedCall);
    const auto decoded = decode_mobile_terminated_call(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->basicCallInformation.has_value());
    ASSERT_TRUE(decoded->basicCallInformation->callOriginator.has_value());
    EXPECT_EQ(*decoded->basicCallInformation->callOriginator->callingNumber, "447700900010");
    EXPECT_EQ(*decoded->basicCallInformation->callOriginator->clirIndicator, 1);
    EXPECT_EQ(*decoded->basicCallInformation->originatingNetwork, "44770");
    ASSERT_TRUE(decoded->equipmentIdentifier.has_value());
    EXPECT_EQ(decoded->equipmentIdentifier->value, "35209900176149");
}

TEST(Tap3SupplService, FullSupplServiceEventRoundTrips) {
    SupplServiceEvent event;
    ChargeableSubscriber sub;
    sub.isSim = true;
    sub.imsi = "999700000000003";
    event.chargeableSubscriber = sub;

    SupplServiceUsed used;
    used.supplServiceCode = "21";
    used.supplServiceActionCode = 1;
    used.ssParameters = "PARAM1";
    ChargeInformation ci;
    ci.chargedItem = "S";
    used.chargeInformation = ci;
    event.supplServiceUsed = used;

    const auto tlv = encode_suppl_service_event(event);
    EXPECT_EQ(tlv.tag_number, Tag::kSupplServiceEvent);
    const auto decoded = decode_suppl_service_event(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->chargeableSubscriber.has_value());
    EXPECT_EQ(*decoded->chargeableSubscriber->imsi, "999700000000003");
    ASSERT_TRUE(decoded->supplServiceUsed.has_value());
    EXPECT_EQ(*decoded->supplServiceUsed->supplServiceCode, "21");
    EXPECT_EQ(*decoded->supplServiceUsed->supplServiceActionCode, 1);
    ASSERT_TRUE(decoded->supplServiceUsed->chargeInformation.has_value());
    EXPECT_EQ(*decoded->supplServiceUsed->chargeInformation->chargedItem, "S");
}

TEST(Tap3Scu, ServiceCentreUsageGsmBranchRoundTrips) {
    ServiceCentreUsage scu;
    ScuBasicInformation basic;
    ScuChargeableSubscriber sub;
    sub.isGsm = true;
    GsmChargeableSubscriber gsm;
    gsm.imsi = "999700000000004";
    gsm.msisdn = "447700900020";
    sub.gsm = gsm;
    basic.chargeableSubscriber = sub;
    basic.chargedPartyStatus = 1;
    scu.basicInformation = basic;
    scu.servingNetwork = "44770";
    scu.recEntityCode = 3;

    const auto tlv = encode_service_centre_usage(scu);
    EXPECT_EQ(tlv.tag_number, Tag::kServiceCentreUsage);
    const auto decoded = decode_service_centre_usage(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->basicInformation.has_value());
    ASSERT_TRUE(decoded->basicInformation->chargeableSubscriber.has_value());
    EXPECT_TRUE(decoded->basicInformation->chargeableSubscriber->isGsm);
    ASSERT_TRUE(decoded->basicInformation->chargeableSubscriber->gsm.has_value());
    EXPECT_EQ(*decoded->basicInformation->chargeableSubscriber->gsm->imsi, "999700000000004");
    EXPECT_EQ(*decoded->basicInformation->chargedPartyStatus, 1);
    EXPECT_EQ(*decoded->servingNetwork, "44770");
}

TEST(Tap3Scu, ScuChargeableSubscriberMinBranchRoundTrips) {
    ScuChargeableSubscriber sub;
    sub.isGsm = false;
    ChargeableSubscriber min_sub;
    min_sub.isSim = false;
    min_sub.min = "5551234567";
    sub.min = min_sub;

    const auto tlv = encode_scu_chargeable_subscriber(sub);
    const auto decoded = decode_scu_chargeable_subscriber(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->isGsm);
    ASSERT_TRUE(decoded->min.has_value());
    EXPECT_EQ(*decoded->min->min, "5551234567");
}

TEST(Tap3GprsCall, FullGprsCallRoundTrips) {
    GprsCall call;
    GprsBasicCallInformation basic;
    GprsChargeableSubscriber gsub;
    ChargeableSubscriber sub;
    sub.isSim = true;
    sub.imsi = "999700000000005";
    gsub.chargeableSubscriber = sub;
    gsub.pdpAddress = "10.0.0.1";
    basic.gprsChargeableSubscriber = gsub;
    basic.chargingId = 9876543210; // exercises the real 8-byte-INTEGER exception path
    basic.totalCallEventDuration = 300;
    call.gprsBasicCallInformation = basic;

    GprsServiceUsed used;
    used.dataVolumeIncoming = 123456789012;
    used.dataVolumeOutgoing = 987654321098;
    call.gprsServiceUsed = used;

    const auto tlv = encode_gprs_call(call);
    EXPECT_EQ(tlv.tag_number, Tag::kGprsCall);
    const auto decoded = decode_gprs_call(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->gprsBasicCallInformation.has_value());
    ASSERT_TRUE(decoded->gprsBasicCallInformation->gprsChargeableSubscriber.has_value());
    ASSERT_TRUE(decoded->gprsBasicCallInformation->gprsChargeableSubscriber->chargeableSubscriber
                    .has_value());
    EXPECT_EQ(
        *decoded->gprsBasicCallInformation->gprsChargeableSubscriber->chargeableSubscriber->imsi,
        "999700000000005");
    EXPECT_EQ(*decoded->gprsBasicCallInformation->chargingId, 9876543210);
    ASSERT_TRUE(decoded->gprsServiceUsed.has_value());
    EXPECT_EQ(*decoded->gprsServiceUsed->dataVolumeIncoming, 123456789012);
    EXPECT_EQ(*decoded->gprsServiceUsed->dataVolumeOutgoing, 987654321098);
}

TEST(Tap3ContentTransaction, FullContentTransactionRoundTrips) {
    ContentTransaction tx;
    ContentTransactionBasicInfo basic;
    basic.transactionStatus = 1;
    tx.contentTransactionBasicInfo = basic;

    ChargedPartyInformation cpi;
    cpi.chargedPartyIdList.push_back(ChargedPartyIdentification{1, std::string("id-1")});
    tx.chargedPartyInformation = cpi;

    ServingPartiesInformation spi;
    spi.contentProviderName = "Acme Content";
    spi.contentProviderIdList.push_back(ContentProvider{2, std::string("cp-1")});
    tx.servingPartiesInformation = spi;

    ContentServiceUsed csu;
    csu.contentTransactionCode = 5;
    csu.totalDataVolume = 555555555555;
    AdvisedChargeInformation aci;
    aci.advisedCharge = 250;
    csu.advisedChargeInformation = aci;
    tx.contentServiceUsed.push_back(csu);

    const auto tlv = encode_content_transaction(tx);
    EXPECT_EQ(tlv.tag_number, Tag::kContentTransaction);
    const auto decoded = decode_content_transaction(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->contentTransactionBasicInfo.has_value());
    EXPECT_EQ(*decoded->contentTransactionBasicInfo->transactionStatus, 1);
    ASSERT_TRUE(decoded->chargedPartyInformation.has_value());
    ASSERT_EQ(decoded->chargedPartyInformation->chargedPartyIdList.size(), 1u);
    EXPECT_EQ(*decoded->chargedPartyInformation->chargedPartyIdList[0].chargedPartyIdentifier,
              "id-1");
    ASSERT_TRUE(decoded->servingPartiesInformation.has_value());
    EXPECT_EQ(*decoded->servingPartiesInformation->contentProviderName, "Acme Content");
    ASSERT_EQ(decoded->contentServiceUsed.size(), 1u);
    EXPECT_EQ(*decoded->contentServiceUsed[0].totalDataVolume, 555555555555);
    ASSERT_TRUE(decoded->contentServiceUsed[0].advisedChargeInformation.has_value());
    EXPECT_EQ(*decoded->contentServiceUsed[0].advisedChargeInformation->advisedCharge, 250);
}

TEST(Tap3LocationService, FullLocationServiceRoundTrips) {
    LocationService loc;
    loc.recEntityCode = 4;
    loc.callReference = std::vector<std::uint8_t>{0x0A, 0x0B};

    TrackingCustomerInformation tci;
    tci.trackingCustomerIdList.push_back(TrackingCustomerIdentification{1, std::string("track-1")});
    loc.trackingCustomerInformation = tci;

    TrackedCustomerInformation tracked;
    tracked.trackedCustomerIdList.push_back(TrackedCustomerIdentification{2, std::string("tgt-1")});
    loc.trackedCustomerInformation = tracked;

    LocationServiceUsage usage;
    LCSQosRequested req;
    req.horizontalAccuracyRequested = 50;
    usage.lcsQosRequested = req;
    LCSQosDelivered del;
    del.ageOfLocation = 3;
    usage.lcsQosDelivered = del;
    loc.locationServiceUsage = usage;

    const auto tlv = encode_location_service(loc);
    EXPECT_EQ(tlv.tag_number, Tag::kLocationService);
    const auto decoded = decode_location_service(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded->recEntityCode, 4);
    ASSERT_TRUE(decoded->trackingCustomerInformation.has_value());
    ASSERT_EQ(decoded->trackingCustomerInformation->trackingCustomerIdList.size(), 1u);
    EXPECT_EQ(*decoded->trackingCustomerInformation->trackingCustomerIdList[0].customerIdentifier,
              "track-1");
    ASSERT_TRUE(decoded->trackedCustomerInformation.has_value());
    ASSERT_EQ(decoded->trackedCustomerInformation->trackedCustomerIdList.size(), 1u);
    EXPECT_EQ(*decoded->trackedCustomerInformation->trackedCustomerIdList[0].customerIdentifier,
              "tgt-1");
    ASSERT_TRUE(decoded->locationServiceUsage.has_value());
    ASSERT_TRUE(decoded->locationServiceUsage->lcsQosRequested.has_value());
    EXPECT_EQ(*decoded->locationServiceUsage->lcsQosRequested->horizontalAccuracyRequested, 50);
    ASSERT_TRUE(decoded->locationServiceUsage->lcsQosDelivered.has_value());
    EXPECT_EQ(*decoded->locationServiceUsage->lcsQosDelivered->ageOfLocation, 3);
}

TEST(Tap3MessagingEvent, FullMessagingEventRoundTrips) {
    MessagingEvent event;
    event.messagingEventService = 1;
    ChargedParty cp;
    cp.imsi = "999700000000006";
    cp.publicUserId = "sip:user@example.com";
    event.chargedParty = cp;
    event.eventReference = "evt-1";
    event.networkElementList.push_back(NetworkElement{1, std::string("ne-1")});
    event.charge = 100;

    const auto tlv = encode_messaging_event(event);
    EXPECT_EQ(tlv.tag_number, Tag::kMessagingEvent);
    const auto decoded = decode_messaging_event(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded->messagingEventService, 1);
    ASSERT_TRUE(decoded->chargedParty.has_value());
    EXPECT_EQ(*decoded->chargedParty->imsi, "999700000000006");
    EXPECT_EQ(*decoded->chargedParty->publicUserId, "sip:user@example.com");
    EXPECT_EQ(*decoded->eventReference, "evt-1");
    ASSERT_EQ(decoded->networkElementList.size(), 1u);
    EXPECT_EQ(*decoded->networkElementList[0].elementId, "ne-1");
    EXPECT_EQ(*decoded->charge, 100);
}

TEST(Tap3MobileSession, FullMobileSessionRoundTrips) {
    MobileSession session;
    session.mobileSessionService = 2;
    ChargedParty cp;
    cp.msisdn = "447700900030";
    session.chargedParty = cp;
    RequestedDestination dest;
    dest.requestedPublicUserId = "sip:dest@example.com";
    session.requestedDestination = dest;
    SessionChargeInformation sci;
    sci.chargedItem = "D";
    session.sessionChargeInfoList.push_back(sci);

    const auto tlv = encode_mobile_session(session);
    EXPECT_EQ(tlv.tag_number, Tag::kMobileSession);
    const auto decoded = decode_mobile_session(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded->mobileSessionService, 2);
    ASSERT_TRUE(decoded->chargedParty.has_value());
    EXPECT_EQ(*decoded->chargedParty->msisdn, "447700900030");
    ASSERT_TRUE(decoded->requestedDestination.has_value());
    EXPECT_EQ(*decoded->requestedDestination->requestedPublicUserId, "sip:dest@example.com");
    ASSERT_EQ(decoded->sessionChargeInfoList.size(), 1u);
    EXPECT_EQ(*decoded->sessionChargeInfoList[0].chargedItem, "D");
}

TEST(Tap3AggregatedUsage, FullAggregatedUsageRecordRoundTrips) {
    AggregatedUsageRecord aur;
    aur.aggregatedUsageDateStart = "20260801";
    aur.aggregatedUsageDateEnd = "20260831";
    aur.servingNetwork = "44770";
    aur.aggregatedUsageCharge = 999999999999; // exercises the real 8-byte-INTEGER exception path
    AURTaxInformation tax;
    tax.taxCode = 1;
    tax.aurTaxValue = 111111111111;
    aur.aurTaxInformationList.push_back(tax);

    const auto tlv = encode_aggregated_usage_record(aur);
    EXPECT_EQ(tlv.tag_number, Tag::kAggregatedUsageRecord);
    const auto decoded = decode_aggregated_usage_record(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded->aggregatedUsageDateStart, "20260801");
    EXPECT_EQ(*decoded->aggregatedUsageDateEnd, "20260831");
    EXPECT_EQ(*decoded->aggregatedUsageCharge, 999999999999);
    ASSERT_EQ(decoded->aurTaxInformationList.size(), 1u);
    EXPECT_EQ(*decoded->aurTaxInformationList[0].aurTaxValue, 111111111111);
}

TEST(Tap3Envelope, CallEventDetailListDispatchesAllRealVariants) {
    CallEventDetailList list;

    MobileOriginatedCall mo;
    list.mobileOriginatedCall.push_back(encode_mobile_originated_call(mo));

    MobileTerminatedCall mt;
    list.mobileTerminatedCall.push_back(encode_mobile_terminated_call(mt));

    SupplServiceEvent sse;
    list.supplServiceEvent.push_back(encode_suppl_service_event(sse));

    ServiceCentreUsage scu;
    list.serviceCentreUsage.push_back(encode_service_centre_usage(scu));

    GprsCall gc;
    list.gprsCall.push_back(encode_gprs_call(gc));

    ContentTransaction ct;
    list.contentTransaction.push_back(encode_content_transaction(ct));

    LocationService ls;
    list.locationService.push_back(encode_location_service(ls));

    MessagingEvent me;
    list.messagingEvent.push_back(encode_messaging_event(me));

    MobileSession ms;
    list.mobileSession.push_back(encode_mobile_session(ms));

    AggregatedUsageRecord aur;
    list.aggregatedUsageRecord.push_back(encode_aggregated_usage_record(aur));

    const auto tlv = encode_call_event_detail_list(list);
    const auto decoded = decode_call_event_detail_list(tlv);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->mobileOriginatedCall.size(), 1u);
    EXPECT_EQ(decoded->mobileTerminatedCall.size(), 1u);
    EXPECT_EQ(decoded->supplServiceEvent.size(), 1u);
    EXPECT_EQ(decoded->serviceCentreUsage.size(), 1u);
    EXPECT_EQ(decoded->gprsCall.size(), 1u);
    EXPECT_EQ(decoded->contentTransaction.size(), 1u);
    EXPECT_EQ(decoded->locationService.size(), 1u);
    EXPECT_EQ(decoded->messagingEvent.size(), 1u);
    EXPECT_EQ(decoded->mobileSession.size(), 1u);
    EXPECT_EQ(decoded->aggregatedUsageRecord.size(), 1u);
    EXPECT_TRUE(decoded->unrecognizedTagNumbers.empty());
}

TEST(Tap3Envelope, DecodeDataInterchangeRejectsMalformedBytes) {
    std::vector<std::uint8_t> garbage = {0xFF, 0x00};
    EXPECT_FALSE(decode_data_interchange(garbage).has_value());
}
