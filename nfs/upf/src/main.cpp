// nfs/upf: UPF (User Plane Function) -- Phase 3 Stage 1 (docs/DECISIONS.md ADR-0039).
// PFCP/N4 server (TS 29.244), UPF's only real protocol interface -- unlike every other NF in this
// project, UPF exposes no SBI service of its own (no Nupf_* API exists in the OpenAPI corpus: real
// 3GPP architecture has SMF talk to UPF exclusively over N4/PFCP, never SBI). UPF's only SBI role
// is as a REGISTRATION CLIENT to NRF (real: NFType=UPF and NFProfile.upfInfo are genuine fields in
// TS29122_CommonData_grp.hpp's generated types, confirmed before writing this, not assumed) so
// SMF can discover it -- Stage 2 wires that discovery up for real; Stage 1's scope here is just
// UPF existing, registering, and answering the two PFCP node-related procedures TS 23.502's PDU
// Session Establishment flow needs before any session can be created: Heartbeat (TS 29.244
// §7.4.2) and Association Setup (§7.4.4.1/§7.4.4.2).
//
// Deliberately deferred, not dropped: Session Establishment/Modification/Deletion (Stage 3),
// Association Update/Release, Node Report, PFD Management -- everything this build doesn't need
// yet. No packet forwarding datapath exists yet either (Stage 4, eBPF/XDP -- ADR-0039).
//
// Disclosed simplification: this build never terminates -- no SIGINT/SIGTERM handling, matching
// every other NF in this project (none of them have graceful shutdown either).

#include "pfcp_core/common_ies.hpp"
#include "pfcp_core/header.hpp"
#include "pfcp_core/ie.hpp"

#include "sbi_core/http2_client.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/oauth2_client.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/sbi_headers.hpp"
#include "sbi_core/uuid.hpp"

#include "TS29122_CommonData_grp.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <thread>

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/upf/CMakeLists.txt)"
#endif

namespace {

using nlohmann::json;

constexpr const char* kNfType = "UPF";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kMetricsBindAddress = "0.0.0.0:9471";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

// This project's only configured S-NSSAI/DNN combination throughout (simulators/ransim/config/
// gnb.yaml's sst=1/sd=1, SMF's own dnn="internet" default) -- reused here so UPF's advertised
// upfInfo genuinely matches what Stage 3's real N4 Session Establishment will ask for, not an
// arbitrary placeholder.
constexpr std::int64_t kSst = 1;
constexpr const char* kSd = "000001";
constexpr const char* kDnn = "internet";

// Same pattern as every other NF's run_nrf_lifecycle (docs/DECISIONS.md ADR-0006/ADR-0019), with
// one real difference: UPF has no HTTP2 server of its own to advertise (see file header) -- this
// is purely an outbound SBI client role.
void run_nrf_lifecycle(const std::string& upf_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/upf/cert.pem",
        .key_path = CERTS_DIR "/upf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client http_client(std::move(client_tls));

    for (int attempt = 0; attempt < 300; ++attempt) {
        sbi_core::http2::ClientRequest probe;
        probe.method = "GET";
        probe.url = std::string(kNrfBase) +
                    "/nnrf-nfm/v1/nf-instances/00000000-0000-4000-8000-000000000000";
        if (http_client.send(probe).has_value()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    sbi_core::OAuth2Client oauth(
        http_client, std::string(kNrfBase) + "/oauth2/token", upf_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;

    sbi_gen::ExtSnssai snssai{};
    snssai.sst = kSst;
    snssai.sd = kSd;
    sbi_gen::DnnUpfInfoItem dnn_info{};
    dnn_info.dnn = kDnn;
    sbi_gen::SnssaiUpfInfoItem snssai_upf_info{};
    snssai_upf_info.sNssai = snssai;
    snssai_upf_info.dnnUpfInfoList = std::vector<sbi_gen::DnnUpfInfoItem>{dnn_info};
    sbi_gen::UpfInfo upf_info{};
    upf_info.sNssaiUpfInfoList = std::vector<sbi_gen::SnssaiUpfInfoItem>{snssai_upf_info};

    json profile{
        {"nfInstanceId", upf_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
        {"upfInfo", json(upf_info)},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("upf: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + upf_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("upf: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("upf: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("upf: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + upf_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("upf: heartbeat failed");
        }
    }
}

// Builds a Heartbeat Response or Association Setup Response's IE region for the given sequence
// number, dispatched by run_pfcp_lifecycle below.
std::vector<std::uint8_t> build_heartbeat_response_ies(std::time_t start_time) {
    std::vector<std::uint8_t> ies;
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::RecoveryTimeStamp),
                         pfcp_core::encode_recovery_time_stamp(start_time));
    return ies;
}

std::vector<std::uint8_t> build_association_setup_response_ies(std::time_t start_time,
                                                                std::array<std::uint8_t, 4> node_ipv4) {
    std::vector<std::uint8_t> ies;
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::NodeId),
                         pfcp_core::encode_node_id_ipv4(node_ipv4));
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::Cause),
                         pfcp_core::encode_cause(pfcp_core::Cause::RequestAccepted));
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::RecoveryTimeStamp),
                         pfcp_core::encode_recovery_time_stamp(start_time));
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::UpFunctionFeatures),
                         pfcp_core::encode_up_function_features_none());
    return ies;
}

// Runs on the main thread (blocking UDP I/O, same "blocking transport gets its own thread"
// discipline ADR-0006/ADR-0030 already established -- here it's simply the only thread, since
// UPF has no HTTP2 server to share time with). Never returns.
void run_pfcp_lifecycle(std::time_t start_time) {
    boost::asio::io_context ioc;
    boost::asio::ip::udp::socket socket(
        ioc, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), pfcp_core::kPfcpPort));
    spdlog::info("upf: listening for PFCP/N4 (UDP) on 0.0.0.0:{}", pfcp_core::kPfcpPort);

    constexpr std::array<std::uint8_t, 4> kNodeIpv4{127, 0, 0, 1}; // this lab's loopback-only scope

    std::vector<std::uint8_t> recv_buf(2048);
    while (true) {
        boost::asio::ip::udp::endpoint sender;
        boost::system::error_code ec;
        const std::size_t n = socket.receive_from(boost::asio::buffer(recv_buf), sender, 0, ec);
        if (ec) {
            spdlog::warn("upf: PFCP receive failed: {}", ec.message());
            continue;
        }
        const std::vector<std::uint8_t> msg(recv_buf.begin(),
                                            recv_buf.begin() + static_cast<std::ptrdiff_t>(n));

        std::size_t offset = 0;
        std::uint16_t ies_length = 0;
        const auto header = pfcp_core::decode_header(msg, offset, ies_length);
        if (!header.has_value()) {
            spdlog::warn("upf: failed to decode PFCP header from {}, ignoring",
                        sender.address().to_string());
            continue;
        }
        if (offset + ies_length > msg.size()) {
            spdlog::warn("upf: PFCP message length field overruns the datagram, ignoring");
            continue;
        }
        const std::vector<std::uint8_t> ie_bytes(
            msg.begin() + static_cast<std::ptrdiff_t>(offset),
            msg.begin() + static_cast<std::ptrdiff_t>(offset + ies_length));

        pfcp_core::Header resp_header;
        resp_header.has_seid = false;
        resp_header.sequence_number = header->sequence_number;
        std::vector<std::uint8_t> resp_ies;

        if (header->message_type == pfcp_core::MessageType::HeartbeatRequest) {
            resp_header.message_type = pfcp_core::MessageType::HeartbeatResponse;
            resp_ies = build_heartbeat_response_ies(start_time);
            spdlog::info("upf: replying to Heartbeat Request from {}", sender.address().to_string());
        } else if (header->message_type == pfcp_core::MessageType::AssociationSetupRequest) {
            resp_header.message_type = pfcp_core::MessageType::AssociationSetupResponse;
            resp_ies = build_association_setup_response_ies(start_time, kNodeIpv4);
            spdlog::info("upf: Sx Association Setup accepted from {}", sender.address().to_string());
        } else {
            spdlog::warn("upf: received PFCP message type {} with no handler yet, ignoring",
                        static_cast<int>(header->message_type));
            continue;
        }

        auto resp_bytes = pfcp_core::encode_header(
            resp_header, static_cast<std::uint16_t>(resp_ies.size()));
        resp_bytes.insert(resp_bytes.end(), resp_ies.begin(), resp_ies.end());
        socket.send_to(boost::asio::buffer(resp_bytes), sender, 0, ec);
        if (ec) {
            spdlog::warn("upf: PFCP send failed: {}", ec.message());
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("upf");
    sbi_core::init_tracing("upf");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string upf_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("upf: starting, nfInstanceId={}", upf_instance_id);
    spdlog::info("upf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);

    const std::time_t start_time = std::time(nullptr);

    std::thread(run_nrf_lifecycle, upf_instance_id).detach();
    run_pfcp_lifecycle(start_time); // blocks forever, main thread
    return 0;
}
