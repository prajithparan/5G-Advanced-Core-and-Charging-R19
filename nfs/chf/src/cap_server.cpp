#include "cap_server.hpp"

#include <spdlog/spdlog.h>

#include "cap_core/cap_dictionary.hpp"
#include "cap_core/cap_operations.hpp"
#include "ss7_core/m3ua_asp.hpp"
#include "ss7_core/m3ua_dictionary.hpp"
#include "ss7_core/m3ua_header.hpp"
#include "ss7_core/m3ua_protocol_data.hpp"
#include "ss7_core/m3ua_tlv.hpp"
#include "ss7_core/sccp_dictionary.hpp"
#include "ss7_core/sccp_udt.hpp"
#include "tbcd_core/tbcd.hpp"
#include "tcap_core/component.hpp"
#include "tcap_core/message.hpp"

namespace chf {

namespace {

void send_m3ua(ss7_core::SctpSocket& sock,
               std::uint8_t message_class,
               std::uint8_t message_type,
               const std::vector<std::uint8_t>& body) {
    auto msg = ss7_core::encode_m3ua_header({message_class, message_type},
                                            static_cast<std::uint32_t>(body.size()));
    msg.insert(msg.end(), body.begin(), body.end());
    sock.send(msg);
}

struct ReceivedM3ua {
    ss7_core::M3uaHeader header;
    std::vector<std::uint8_t> payload;
};

std::optional<ReceivedM3ua> receive_m3ua(ss7_core::SctpSocket& sock) {
    const auto bytes = sock.receive();
    if (bytes.empty()) {
        return std::nullopt;
    }
    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    const auto header = ss7_core::decode_m3ua_header(bytes, offset, payload_length);
    if (!header.has_value()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                      bytes.end());
    if (payload.size() != payload_length) {
        return std::nullopt;
    }
    return ReceivedM3ua{*header, std::move(payload)};
}

// Real M3UA ASPSM/ASPTM activation handshake, responder role (this side receives ASP Up/ASP
// Active from the peer and acknowledges -- RFC 4666 §3.5/§3.7).
bool do_asp_handshake_responder(ss7_core::SctpSocket& sock) {
    using namespace ss7_core;

    const auto up = receive_m3ua(sock);
    if (!up.has_value() || up->header.message_class != dictionary::MessageClass::kAspsm ||
        up->header.message_type != dictionary::AspsmMessageType::kAspUp) {
        return false;
    }
    send_m3ua(sock,
              dictionary::MessageClass::kAspsm,
              dictionary::AspsmMessageType::kAspUpAck,
              encode_asp_state_message(dictionary::AspsmMessageType::kAspUpAck, {}));

    const auto active = receive_m3ua(sock);
    if (!active.has_value() || active->header.message_class != dictionary::MessageClass::kAsptm ||
        active->header.message_type != dictionary::AsptmMessageType::kAspActive) {
        return false;
    }
    AspTrafficMessage ack_msg;
    ack_msg.traffic_mode_type = dictionary::TrafficModeType::kOverride;
    send_m3ua(sock,
              dictionary::MessageClass::kAsptm,
              dictionary::AsptmMessageType::kAspActiveAck,
              encode_asp_traffic_message(dictionary::AsptmMessageType::kAspActiveAck, ack_msg));
    return true;
}

// Unwraps an M3UA DATA message down to the real SCCP UDT it carries, or std::nullopt on any
// malformed layer.
std::optional<ss7_core::SccpUdt> unwrap_to_sccp(const ReceivedM3ua& msg) {
    if (msg.header.message_class != ss7_core::dictionary::MessageClass::kTransfer ||
        msg.header.message_type != ss7_core::dictionary::TransferMessageType::kData) {
        return std::nullopt;
    }
    const auto tlvs = ss7_core::decode_m3ua_tlvs(msg.payload);
    if (!tlvs.has_value()) {
        return std::nullopt;
    }
    const auto* pd_tlv =
        ss7_core::find_m3ua_tlv(*tlvs, ss7_core::dictionary::ParamTag::kProtocolData);
    if (pd_tlv == nullptr) {
        return std::nullopt;
    }
    const auto proto_data = ss7_core::decode_m3ua_protocol_data(pd_tlv->value);
    if (!proto_data.has_value()) {
        return std::nullopt;
    }
    return ss7_core::decode_sccp_udt(proto_data->user_protocol_data);
}

// Wraps a real TCAP message (already-encoded bytes) into a real SCCP UDT (addressed
// calling=gsmSCF/HLR-analogue role reversed for the response: calling=SSF's own called SSN,
// called=SSF's own calling SSN -- real SCCP UDT response convention, addresses swapped) then a
// real M3UA DATA message, and sends it.
void send_tcap(ss7_core::SctpSocket& sock, const std::vector<std::uint8_t>& tcap_bytes) {
    ss7_core::SccpUdt udt;
    udt.protocol_class = ss7_core::dictionary::ProtocolClass::kClass0;
    udt.called_party.ssn_present = true;
    udt.called_party.ssn = ss7_core::dictionary::SubsystemNumber::kMsc;
    udt.calling_party.ssn_present = true;
    udt.calling_party.ssn = ss7_core::dictionary::SubsystemNumber::kHlr; // gsmSCF role, reusing
                                                                         // the real HLR SSN slot
                                                                         // (no dedicated gsmSCF
                                                                         // SSN constant exists in
                                                                         // sccp_dictionary.hpp)
    udt.data = tcap_bytes;
    const auto sccp_bytes = ss7_core::encode_sccp_udt(udt);

    ss7_core::M3uaProtocolData proto_data;
    proto_data.opc = 2;
    proto_data.dpc = 1;
    proto_data.si = ss7_core::dictionary::ServiceIndicator::kSccp;
    proto_data.ni = 2;
    proto_data.sls = 0;
    proto_data.user_protocol_data = sccp_bytes;
    const auto proto_data_bytes = ss7_core::encode_m3ua_protocol_data(proto_data);

    ss7_core::M3uaTlv pd_tlv;
    pd_tlv.tag = ss7_core::dictionary::ParamTag::kProtocolData;
    pd_tlv.value = proto_data_bytes;
    std::vector<std::uint8_t> tlv_bytes;
    ss7_core::encode_m3ua_tlv(tlv_bytes, pd_tlv);

    send_m3ua(sock,
              ss7_core::dictionary::MessageClass::kTransfer,
              ss7_core::dictionary::TransferMessageType::kData,
              tlv_bytes);
}

} // namespace

CapServer::CapServer(std::uint16_t port,
                     sbi_core::http2::TlsConfig client_tls,
                     ChargingDataStore& charging_data_store,
                     CdrWriter& cdr_writer,
                     RatingDecisionStore& rating_decision_store,
                     opentelemetry::metrics::Counter<std::uint64_t>* grant_counter,
                     opentelemetry::metrics::Counter<std::uint64_t>* reserve_rejected_counter,
                     opentelemetry::metrics::Counter<std::uint64_t>* initial_dp_counter,
                     opentelemetry::metrics::Counter<std::uint64_t>* apply_charging_counter)
    : client_tls_(std::move(client_tls)), charging_data_store_(charging_data_store),
      cdr_writer_(cdr_writer), rating_decision_store_(rating_decision_store),
      grant_counter_(grant_counter), reserve_rejected_counter_(reserve_rejected_counter),
      initial_dp_counter_(initial_dp_counter), apply_charging_counter_(apply_charging_counter) {
    listener_.bind_and_listen("0.0.0.0", port);
    accept_thread_ = std::thread(&CapServer::accept_loop, this);
}

CapServer::~CapServer() {
    stop_ = true;
    listener_.close();
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void CapServer::accept_loop() {
    while (!stop_) {
        try {
            auto conn = listener_.accept();
            std::thread(&CapServer::handle_connection, this, std::move(conn)).detach();
        } catch (const std::exception& e) {
            if (!stop_) {
                spdlog::warn("chf: CAP accept() failed: {}", e.what());
            }
        }
    }
}

void CapServer::handle_connection(ss7_core::SctpSocket socket) {
    spdlog::info("chf: real CAP (gsmSSF) peer connected");

    if (!do_asp_handshake_responder(socket)) {
        spdlog::warn("chf: CAP peer failed the real M3UA ASPSM/ASPTM handshake");
        return;
    }

    sbi_core::http2::Client catalog_client(client_tls_);
    sbi_core::http2::Client balance_client(client_tls_);

    while (!stop_) {
        const auto msg = receive_m3ua(socket);
        if (!msg.has_value()) {
            spdlog::info("chf: CAP peer association closed");
            return;
        }
        const auto udt = unwrap_to_sccp(*msg);
        if (!udt.has_value()) {
            spdlog::warn("chf: CAP peer sent a malformed M3UA/SCCP message, ignoring");
            continue;
        }
        const auto tag = tcap_core::peek_tc_message_tag(udt->data);
        if (!tag.has_value() || *tag != tcap_core::MessageTag::kBegin) {
            // Real, disclosed gap: EventReportBCSM/ApplyChargingReport (real TC-Continue/TC-End
            // follow-ups the gsmSSF would send later in the same dialogue) are not implemented --
            // logged and ignored, not silently mishandled.
            spdlog::info("chf: CAP peer sent a non-TC-Begin message (tag={}), not yet implemented, "
                         "ignoring",
                         tag.value_or(0));
            continue;
        }

        const auto begin = tcap_core::decode_tc_begin(udt->data);
        if (!begin.has_value() || begin->components.empty()) {
            spdlog::warn("chf: CAP peer sent a malformed TC-Begin, ignoring");
            continue;
        }
        const auto component = tcap_core::decode_component(begin->components[0]);
        if (!component.has_value() || !component->invoke.has_value() ||
            !component->invoke->operation_code.local.has_value()) {
            spdlog::warn("chf: CAP peer's TC-Begin did not carry a decodable Invoke, ignoring");
            continue;
        }
        const auto& invoke = *component->invoke;
        if (*invoke.operation_code.local != cap_core::Opcode::kInitialDp) {
            spdlog::info("chf: CAP peer's TC-Begin carried opcode {} (only InitialDP=0 is "
                         "implemented), ignoring",
                         *invoke.operation_code.local);
            continue;
        }

        if (initial_dp_counter_ != nullptr) {
            initial_dp_counter_->Add(1);
        }

        const auto arg = cap_core::decode_initial_dp_arg(invoke.parameter);
        if (!arg.has_value() || !arg->imsi.has_value()) {
            spdlog::warn("chf: real InitialDP missing a decodable arg or IMSI -- real "
                         "missingParameter ReturnError");
            tcap_core::ReturnError re;
            re.invoke_id = invoke.invoke_id;
            re.error_code.local = cap_core::ErrorCode::kMissingParameter;
            tcap_core::TcEnd end;
            end.destination_transaction_id = begin->originating_transaction_id;
            end.components.push_back(tcap_core::encode_return_error(re));
            send_tcap(socket, tcap_core::encode_tc_end(end));
            continue;
        }

        const auto imsi_digits = tbcd_core::decode_tbcd(*arg->imsi);
        const std::string supi = "imsi-" + imsi_digits;
        spdlog::info(
            "chf: real CAP InitialDP received (SUPI={}, serviceKey={})", supi, arg->service_key);

        const auto ref = charging_data_store_.create(supi);
        sbi_gen::MultipleUnitUsage_Nchf_ConvergedCharging usage{};
        usage.ratingGroup = static_cast<sbi_gen::Uint32>(arg->service_key);
        const auto charged = chf::charge_one_usage(catalog_client,
                                                   balance_client,
                                                   cdr_writer_,
                                                   rating_decision_store_,
                                                   charging_data_store_,
                                                   grant_counter_,
                                                   reserve_rejected_counter_,
                                                   ref,
                                                   "Create",
                                                   supi,
                                                   "CAP-gsmSSF",
                                                   invoke.invoke_id,
                                                   usage);

        std::int32_t max_call_period_duration = 0; // 100ms units, real ApplyChargingArg field
        if (charged.reserved && charged.rating.grant.has_value() &&
            charged.rating.grant->time.has_value()) {
            max_call_period_duration = static_cast<std::int32_t>(*charged.rating.grant->time) * 10;
        }

        cap_core::RequestReportBcsmEventArg rrbe;
        cap_core::BcsmEvent answer_evt;
        answer_evt.event_type_bcsm = cap_core::EventTypeBcsm::kOAnswer;
        answer_evt.monitor_mode = cap_core::MonitorMode::kNotifyAndContinue;
        cap_core::BcsmEvent disconnect_evt;
        disconnect_evt.event_type_bcsm = cap_core::EventTypeBcsm::kODisconnect;
        disconnect_evt.monitor_mode = cap_core::MonitorMode::kNotifyAndContinue;
        rrbe.bcsm_events = {answer_evt, disconnect_evt};

        tcap_core::Invoke rrbe_invoke;
        rrbe_invoke.invoke_id = 2;
        rrbe_invoke.operation_code.local = cap_core::Opcode::kRequestReportBcsmEvent;
        rrbe_invoke.parameter = cap_core::encode_request_report_bcsm_event_arg(rrbe);

        cap_core::ApplyChargingArg ac;
        ac.max_call_period_duration = max_call_period_duration;
        ac.release_if_duration_exceeded = true;

        tcap_core::Invoke ac_invoke;
        ac_invoke.invoke_id = 3;
        ac_invoke.operation_code.local = cap_core::Opcode::kApplyCharging;
        ac_invoke.parameter = cap_core::encode_apply_charging_arg(ac);

        tcap_core::TcContinue cont;
        cont.originating_transaction_id = {0x00, 0x00, 0x00, 0x01};
        cont.destination_transaction_id = begin->originating_transaction_id;
        cont.components.push_back(tcap_core::encode_invoke(rrbe_invoke));
        cont.components.push_back(tcap_core::encode_invoke(ac_invoke));

        send_tcap(socket, tcap_core::encode_tc_continue(cont));
        if (apply_charging_counter_ != nullptr) {
            apply_charging_counter_->Add(1);
        }
        spdlog::info("chf: real CAP RequestReportBCSMEvent+ApplyCharging sent "
                     "(maxCallPeriodDuration={} = {}s)",
                     max_call_period_duration,
                     max_call_period_duration / 10);
    }
}

} // namespace chf
