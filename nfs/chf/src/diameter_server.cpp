#include "diameter_server.hpp"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <spdlog/spdlog.h>

#include "diameter_core/avp.hpp"
#include "diameter_core/dictionary.hpp"
#include "diameter_core/header.hpp"

namespace chf {

namespace {

using namespace diameter_core;

// This project's own lab-internal Diameter identity -- disclosed, project-internal convention
// (no real IANA-assigned enterprise number, no real registered DNS realm), matching the same
// per-NF-name convention already used for TLS cert CNs (scripts/gen-lab-pki.sh's `-subj
// "/O=5gc-r19 Lab/CN=${nf}"`).
constexpr std::uint32_t kNoVendorId = 0;

std::vector<std::uint8_t> build_cea(const Header& request_header,
                                    const std::string& origin_host,
                                    const std::string& origin_realm,
                                    std::int32_t result_code) {
    std::vector<std::uint8_t> avps_bytes;

    Avp result_code_avp;
    result_code_avp.code = dictionary::Avp::kResultCode;
    result_code_avp.flags = AvpFlag::kMandatory;
    result_code_avp.data = encode_integer32(result_code);
    encode_avp(avps_bytes, result_code_avp);

    Avp origin_host_avp;
    origin_host_avp.code = dictionary::Avp::kOriginHost;
    origin_host_avp.flags = AvpFlag::kMandatory;
    origin_host_avp.data = encode_octet_string(origin_host);
    encode_avp(avps_bytes, origin_host_avp);

    Avp origin_realm_avp;
    origin_realm_avp.code = dictionary::Avp::kOriginRealm;
    origin_realm_avp.flags = AvpFlag::kMandatory;
    origin_realm_avp.data = encode_octet_string(origin_realm);
    encode_avp(avps_bytes, origin_realm_avp);

    // Host-IP-Address is real-spec-REQUIRED (1*) in a real CEA -- this project's own lab
    // convention is loopback-only (every NF binds 0.0.0.0/connects via 127.0.0.1, e.g. CHF's own
    // HTTP/2 listener), so 127.0.0.1 is sent, not a placeholder.
    Avp host_ip_avp;
    host_ip_avp.code = dictionary::Avp::kHostIpAddress;
    host_ip_avp.flags = AvpFlag::kMandatory;
    host_ip_avp.data = encode_address_ipv4((127u << 24) | 1u);
    encode_avp(avps_bytes, host_ip_avp);

    Avp vendor_id_avp;
    vendor_id_avp.code = dictionary::Avp::kVendorId;
    vendor_id_avp.flags = AvpFlag::kMandatory;
    vendor_id_avp.data = encode_unsigned32(kNoVendorId);
    encode_avp(avps_bytes, vendor_id_avp);

    Avp product_name_avp;
    product_name_avp.code = dictionary::Avp::kProductName;
    product_name_avp.flags =
        0; // real spec: Product-Name is NOT flagged Mandatory (dict_base_proto.c)
    product_name_avp.data = encode_octet_string("5gc-r19-chf");
    encode_avp(avps_bytes, product_name_avp);

    // Auth-Application-Id=4: real RFC 4006 Diameter Credit-Control Application ID -- the only
    // application this CHF's Diameter server will ever accept (Stage 3's Gy CCR/CCA), advertised
    // here so a real peer's own CER/CEA capability negotiation can see it.
    Avp auth_app_id_avp;
    auth_app_id_avp.code = dictionary::Avp::kAuthApplicationId;
    auth_app_id_avp.flags = AvpFlag::kMandatory;
    auth_app_id_avp.data = encode_unsigned32(dictionary::Dcc::kApplicationId);
    encode_avp(avps_bytes, auth_app_id_avp);

    Header answer_header;
    answer_header.flags = 0; // R bit cleared: this is an Answer, not a Request
    answer_header.command_code = dictionary::Command::kCapabilitiesExchange;
    answer_header.application_id = 0; // CER/CEA itself always uses Application-Id 0
    answer_header.hop_by_hop_id = request_header.hop_by_hop_id; // real spec: echoed from request
    answer_header.end_to_end_id = request_header.end_to_end_id; // real spec: echoed from request

    auto message = encode_header(answer_header, static_cast<std::uint32_t>(avps_bytes.size()));
    message.insert(message.end(), avps_bytes.begin(), avps_bytes.end());
    return message;
}

} // namespace

DiameterServer::DiameterServer(std::uint16_t port,
                               std::string origin_host,
                               std::string origin_realm)
    : ioc_(), acceptor_(ioc_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      origin_host_(std::move(origin_host)), origin_realm_(std::move(origin_realm)) {
    accept_thread_ = std::thread(&DiameterServer::accept_loop, this);
}

DiameterServer::~DiameterServer() {
    stop_ = true;
    boost::system::error_code ec;
    acceptor_.close(ec);
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void DiameterServer::accept_loop() {
    while (!stop_) {
        boost::asio::ip::tcp::socket socket(ioc_);
        boost::system::error_code ec;
        acceptor_.accept(socket, ec);
        if (ec) {
            if (!stop_) {
                spdlog::warn("chf: Diameter accept() failed: {}", ec.message());
            }
            continue;
        }
        std::thread(&DiameterServer::handle_connection, this, std::move(socket)).detach();
    }
}

void DiameterServer::handle_connection(boost::asio::ip::tcp::socket socket) {
    boost::system::error_code ec;
    const auto peer = socket.remote_endpoint(ec);
    spdlog::info("chf: Diameter peer connected: {}",
                 ec ? std::string("unknown") : peer.address().to_string());

    std::vector<std::uint8_t> header_bytes(20);
    boost::asio::read(socket, boost::asio::buffer(header_bytes), ec);
    if (ec) {
        spdlog::warn("chf: Diameter connection closed before a full header arrived: {}",
                     ec.message());
        return;
    }

    std::size_t offset = 0;
    std::uint32_t avps_length = 0;
    const auto header = decode_header(header_bytes, offset, avps_length);
    if (!header.has_value()) {
        spdlog::warn("chf: Diameter peer sent a malformed message header, closing connection");
        return;
    }

    std::vector<std::uint8_t> avps_bytes(avps_length);
    if (avps_length > 0) {
        boost::asio::read(socket, boost::asio::buffer(avps_bytes), ec);
        if (ec) {
            spdlog::warn("chf: Diameter connection closed before AVPs arrived: {}", ec.message());
            return;
        }
    }

    if (header->command_code != dictionary::Command::kCapabilitiesExchange ||
        (header->flags & CommandFlag::kRequest) == 0) {
        // Stage 2 scope (ADR-0059): only the CER/CEA handshake is handled. Anything else (real
        // CCR, or a peer that skips CER entirely) gets the connection closed -- Stage 3 replaces
        // this branch with the real CCR/CCA dispatch loop.
        spdlog::warn(
            "chf: Diameter peer's first message was not a CER (command_code={}, flags={:#x}) -- "
            "closing connection (Stage 2 only handles capability exchange, ADR-0059)",
            header->command_code,
            header->flags);
        return;
    }

    const auto avps = decode_avps(avps_bytes);
    if (!avps.has_value()) {
        spdlog::warn("chf: Diameter CER had malformed AVPs, closing connection");
        return;
    }

    const auto* peer_origin_host = find_avp(*avps, dictionary::Avp::kOriginHost);
    const auto* peer_origin_realm = find_avp(*avps, dictionary::Avp::kOriginRealm);
    if (peer_origin_host == nullptr || peer_origin_realm == nullptr) {
        spdlog::warn("chf: Diameter CER missing mandatory Origin-Host/Origin-Realm");
        const auto cea = build_cea(
            *header, origin_host_, origin_realm_, dictionary::ResultCode::kDiameterMissingAvp);
        boost::asio::write(socket, boost::asio::buffer(cea), ec);
        return;
    }

    spdlog::info("chf: real CER received from Origin-Host={}",
                 decode_octet_string(peer_origin_host->data).value_or("?"));

    const auto cea =
        build_cea(*header, origin_host_, origin_realm_, dictionary::ResultCode::kDiameterSuccess);
    boost::asio::write(socket, boost::asio::buffer(cea), ec);
    if (ec) {
        spdlog::warn("chf: failed to send CEA: {}", ec.message());
        return;
    }
    spdlog::info("chf: real CEA sent (DIAMETER_SUCCESS) -- Stage 2 (ADR-0059) closes the "
                 "connection here; Stage 3 keeps it open for CCR/CCA");
}

} // namespace chf
