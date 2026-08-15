#include "map_client.hpp"

#include "map_core/map_dictionary.hpp"
#include "ss7_core/m3ua_asp.hpp"
#include "ss7_core/m3ua_dictionary.hpp"
#include "ss7_core/m3ua_header.hpp"
#include "ss7_core/m3ua_protocol_data.hpp"
#include "ss7_core/m3ua_tlv.hpp"
#include "ss7_core/sccp_dictionary.hpp"
#include "ss7_core/sccp_udt.hpp"
#include "ss7_core/sctp_socket.hpp"
#include "tcap_core/component.hpp"
#include "tcap_core/message.hpp"

namespace udm {

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

// Real M3UA ASPSM/ASPTM activation handshake, client role (this side sends ASP Up/ASP Active,
// same real convention the Application Server Process side uses toward the Signalling Gateway --
// RFC 4666 §3.5/§3.7).
bool do_asp_handshake(ss7_core::SctpSocket& sock) {
    using namespace ss7_core;

    send_m3ua(sock,
              dictionary::MessageClass::kAspsm,
              dictionary::AspsmMessageType::kAspUp,
              encode_asp_state_message(dictionary::AspsmMessageType::kAspUp, {}));
    const auto up_ack = receive_m3ua(sock);
    if (!up_ack.has_value() || up_ack->header.message_class != dictionary::MessageClass::kAspsm ||
        up_ack->header.message_type != dictionary::AspsmMessageType::kAspUpAck) {
        return false;
    }

    AspTrafficMessage active_msg;
    active_msg.traffic_mode_type = dictionary::TrafficModeType::kOverride;
    send_m3ua(sock,
              dictionary::MessageClass::kAsptm,
              dictionary::AsptmMessageType::kAspActive,
              encode_asp_traffic_message(dictionary::AsptmMessageType::kAspActive, active_msg));
    const auto active_ack = receive_m3ua(sock);
    if (!active_ack.has_value() ||
        active_ack->header.message_class != dictionary::MessageClass::kAsptm ||
        active_ack->header.message_type != dictionary::AsptmMessageType::kAspActiveAck) {
        return false;
    }
    return true;
}

} // namespace

bool send_insert_subscriber_data(const std::string& peer_address,
                                 std::uint16_t peer_port,
                                 const map_core::InsertSubscriberDataArg& arg) {
    ss7_core::SctpSocket sock;
    sock.connect(peer_address, peer_port);

    if (!do_asp_handshake(sock)) {
        return false;
    }

    tcap_core::Invoke invoke;
    invoke.invoke_id = 1;
    invoke.operation_code.local = map_core::Opcode::kInsertSubscriberData;
    invoke.parameter = map_core::encode_insert_subscriber_data_arg(arg);

    tcap_core::TcBegin begin;
    begin.originating_transaction_id = {0x00, 0x00, 0x00, 0x01};
    begin.components.push_back(tcap_core::encode_invoke(invoke));
    const auto tcap_bytes = tcap_core::encode_tc_begin(begin);

    ss7_core::SccpUdt udt;
    udt.protocol_class = ss7_core::dictionary::ProtocolClass::kClass0;
    udt.called_party.ssn_present = true;
    udt.called_party.ssn = ss7_core::dictionary::SubsystemNumber::kVlr;
    udt.calling_party.ssn_present = true;
    udt.calling_party.ssn = ss7_core::dictionary::SubsystemNumber::kHlr;
    udt.data = tcap_bytes;
    const auto sccp_bytes = ss7_core::encode_sccp_udt(udt);

    ss7_core::M3uaProtocolData proto_data;
    proto_data.opc = 1;
    proto_data.dpc = 2;
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

    const auto response = receive_m3ua(sock);
    if (!response.has_value() ||
        response->header.message_class != ss7_core::dictionary::MessageClass::kTransfer ||
        response->header.message_type != ss7_core::dictionary::TransferMessageType::kData) {
        return false;
    }

    const auto resp_tlvs = ss7_core::decode_m3ua_tlvs(response->payload);
    if (!resp_tlvs.has_value()) {
        return false;
    }
    const auto* resp_pd_tlv =
        ss7_core::find_m3ua_tlv(*resp_tlvs, ss7_core::dictionary::ParamTag::kProtocolData);
    if (resp_pd_tlv == nullptr) {
        return false;
    }
    const auto resp_proto_data = ss7_core::decode_m3ua_protocol_data(resp_pd_tlv->value);
    if (!resp_proto_data.has_value()) {
        return false;
    }

    const auto resp_udt = ss7_core::decode_sccp_udt(resp_proto_data->user_protocol_data);
    if (!resp_udt.has_value()) {
        return false;
    }

    const auto msg_tag = tcap_core::peek_tc_message_tag(resp_udt->data);
    if (!msg_tag.has_value()) {
        return false;
    }

    std::vector<tcap_core::Tlv> components;
    if (*msg_tag == tcap_core::MessageTag::kEnd) {
        const auto end = tcap_core::decode_tc_end(resp_udt->data);
        if (!end.has_value()) {
            return false;
        }
        components = end->components;
    } else if (*msg_tag == tcap_core::MessageTag::kContinue) {
        const auto cont = tcap_core::decode_tc_continue(resp_udt->data);
        if (!cont.has_value()) {
            return false;
        }
        components = cont->components;
    } else {
        return false;
    }

    if (components.empty()) {
        return false;
    }
    const auto component = tcap_core::decode_component(components[0]);
    if (!component.has_value()) {
        return false;
    }

    if (component->return_result_last.has_value() &&
        component->return_result_last->result.has_value()) {
        return map_core::decode_insert_subscriber_data_res(
            component->return_result_last->result->parameter);
    }
    if (component->return_result.has_value() && component->return_result->result.has_value()) {
        return map_core::decode_insert_subscriber_data_res(
            component->return_result->result->parameter);
    }
    return false; // ReturnError or Reject -> a real, disclosed failure outcome, not an error
}

} // namespace udm
