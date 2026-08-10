// nfs/upf: UPF (User Plane Function) -- Phase 3 Stages 1+3 (docs/DECISIONS.md ADR-0039/ADR-0042).
// PFCP/N4 server (TS 29.244), UPF's only real protocol interface -- unlike every other NF in this
// project, UPF exposes no SBI service of its own (no Nupf_* API exists in the OpenAPI corpus: real
// 3GPP architecture has SMF talk to UPF exclusively over N4/PFCP, never SBI). UPF's only SBI role
// is as a REGISTRATION CLIENT to NRF (real: NFType=UPF and NFProfile.upfInfo are genuine fields in
// TS29122_CommonData_grp.hpp's generated types, confirmed before writing this, not assumed) so
// SMF can discover it -- Stage 2 wires that discovery up for real.
//
// Implements: Heartbeat (§7.4.2), Association Setup (§7.4.4.1/§7.4.4.2), and Session
// Establishment (§7.5.2/§7.5.3, ADR-0042) -- specifically one uplink PDR/FAR pair per session
// (Source Interface=Access with a UP-allocated F-TEID, forwarding to Core), the minimal real
// slice TS 23.502's PDU Session Establishment needs. No downlink PDR/FAR: that needs the gNB's
// N3 GTP-U endpoint, which requires NGAP PDU Session Resource Setup (still not implemented, a
// disclosed gap predating this turn -- ADR-0038's own N2 SM info note).
//
// Deliberately deferred, not dropped: Session Modification/Deletion, Association Update/Release,
// Node Report, PFD Management, QER/URR (QoS enforcement/usage reporting) -- everything this build
// doesn't need yet. No packet forwarding datapath exists yet either (Stage 4, eBPF/XDP --
// ADR-0039); Session Establishment here allocates a real F-TEID and echoes it back, but no packet
// ever actually flows through it -- disclosed, not silently implied to work end-to-end.
//
// Disclosed simplification: this build never terminates -- no SIGINT/SIGTERM handling, matching
// every other NF in this project (none of them have graceful shutdown either).
//
// ADR-0050 Stage 2 update: a minimal per-TEID session map (TeidSessionStore below) now DOES exist
// -- SMF's real address and this session's CP F-SEID/URR ID, remembered only for as long as it
// takes to address a real, unsolicited Sx Session Report Request back to SMF once the datapath's
// real per-TEID byte counter (nfs/upf/bpf/gtpu_decap.bpf.c) crosses a provisioned Volume
// Threshold/Quota. Still in-memory only, still lost on restart -- no real Session
// Modification/Deletion exists yet to ever remove an entry either (a real, disclosed gap, not
// urgent while this project has no process-restart/session-teardown testing).

#include "datapath.hpp"

#include "pfcp_core/common_ies.hpp"
#include "pfcp_core/header.hpp"
#include "pfcp_core/ie.hpp"
#include "pfcp_core/session_ies.hpp"

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

#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#include <unordered_map>

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

// ADR-0050 Stage 2: what UPF needs to remember, per allocated uplink TEID, to address a real
// unsolicited Sx Session Report Request back to SMF once a Volume Threshold/Quota crossing fires.
// Populated by run_pfcp_lifecycle's main thread on Session Establishment; read (and its per-URR
// UR-SEQN counter advanced) from Datapath's own ring-buffer-polling thread whenever the usage
// report handler fires -- hence the mutex, unlike every other piece of session-establishment state
// in this file, which never leaves run_pfcp_lifecycle's single thread.
struct UrrSessionInfo {
    boost::asio::ip::udp::endpoint smf_endpoint;
    std::uint64_t cp_seid = 0;
    std::uint32_t urr_id = 0;
};

class TeidSessionStore {
public:
    void put(std::uint32_t teid, UrrSessionInfo info) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[teid] = info;
        next_ur_seqn_[teid] = 1; // TS 29.244 UR-SEQN: this project's own per-URR counter, not the
                                 // PFCP header's separate node-level Sequence Number (see
                                 // usage_report_handler's own comment on that distinction).
    }

    // Returns the session info plus the UR-SEQN to use for this report, advancing the counter for
    // next time. std::nullopt if no Create URR was ever provisioned for this TEID (e.g. the usage
    // report handler firing for a TEID this store never learned about -- shouldn't happen given
    // the datapath only counts TEIDs register_urr was called for, but checked rather than assumed).
    std::optional<std::pair<UrrSessionInfo, std::uint32_t>> get_and_advance_seqn(std::uint32_t teid) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sessions_.find(teid);
        if (it == sessions_.end()) {
            return std::nullopt;
        }
        const std::uint32_t seqn = next_ur_seqn_[teid]++;
        return std::make_pair(it->second, seqn);
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::uint32_t, UrrSessionInfo> sessions_;
    std::unordered_map<std::uint32_t, std::uint32_t> next_ur_seqn_;
};

// ADR-0050 Stage 2: a small, dedicated UDP socket the usage report handler uses (from Datapath's
// own polling thread) to fire-and-forget a Session Report Request to SMF. Deliberately separate
// from run_pfcp_lifecycle's own receive socket -- that one is only ever touched by the main
// thread; giving the datapath thread its own socket avoids needing to reason about concurrent
// send/receive on one boost::asio::ip::udp::socket from two threads at once.
class ReportSender {
public:
    ReportSender() : socket_(ioc_, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)) {}

    void send(const boost::asio::ip::udp::endpoint& target, const std::vector<std::uint8_t>& bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(bytes), target, 0, ec);
        if (ec) {
            spdlog::warn("upf: failed to send unsolicited PFCP message to {}: {}",
                        target.address().to_string(), ec.message());
        }
    }

private:
    boost::asio::io_context ioc_;
    boost::asio::ip::udp::socket socket_;
    std::mutex mutex_;
};

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

// TS 29.244 Table 8.2.25-1, feature octet 5 bit 5 (FTUP): "F-TEID allocation / release in the UP
// function is supported by the UP function" -- true now that Stage 3 (ADR-0042) actually allocates
// F-TEIDs on CH request, so this must say so honestly rather than staying the all-zero
// "no optional features" bitmask Stage 1 originally sent.
std::vector<std::uint8_t> encode_up_function_features_ftup_only() {
    constexpr std::uint8_t kFtupBit = 0x10; // octet 5, bit 5
    return {kFtupBit, 0x00};
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
                         encode_up_function_features_ftup_only());
    return ies;
}

struct SessionEstablishmentResult {
    std::vector<std::uint8_t> ies;
    // The header SEID for this response: TS 29.244's addressing rule ("the sending entity uses
    // the SEID value provided by the corresponding receiving entity") means UPF's response header
    // must carry the value the CP F-SEID IE in the request said to use -- not UPF's own new SEID.
    std::uint64_t response_header_seid = 0;
    // ADR-0050 Stage 2: set only when this session allocated a real uplink F-TEID AND the request
    // carried a real Create URR for it -- the two pieces of information run_pfcp_lifecycle needs
    // to remember this session in TeidSessionStore (so a later usage report can be addressed back
    // to SMF). std::nullopt in every other case (no F-TEID allocated, or no URR requested).
    std::optional<std::uint32_t> allocated_teid_with_urr;
    std::optional<std::uint32_t> urr_id;
};

// Session Establishment (TS 29.244 §7.5.2/§7.5.3, ADR-0042). Decodes the CP F-SEID and the first
// Create PDR/Create FAR pair, allocates a real local F-TEID (if the PDI's F-TEID requested CH)
// and a real UP F-SEID, and builds the response. `next_seid`/`next_teid` are simple counters --
// safe as plain (non-atomic) locals since this function only ever runs on run_pfcp_lifecycle's
// single thread.
std::optional<SessionEstablishmentResult> build_session_establishment_response_ies(
    const std::vector<std::uint8_t>& request_ies, std::array<std::uint8_t, 4> node_ipv4,
    std::uint64_t& next_seid, std::uint32_t& next_teid, upf::Datapath* datapath) {
    const auto ies = pfcp_core::decode_ies(request_ies);
    if (!ies.has_value()) {
        return std::nullopt;
    }
    const auto* cp_f_seid_ie =
        pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::FSeid));
    const auto* create_pdr_ie =
        pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreatePdr));
    if (cp_f_seid_ie == nullptr || create_pdr_ie == nullptr) {
        spdlog::warn("upf: Session Establishment Request missing mandatory CP F-SEID or Create PDR");
        return std::nullopt;
    }
    const auto cp_f_seid = pfcp_core::decode_f_seid_ipv4(cp_f_seid_ie->value);
    if (!cp_f_seid.has_value()) {
        spdlog::warn("upf: Session Establishment Request has a malformed CP F-SEID");
        return std::nullopt;
    }

    const auto pdr_ies = pfcp_core::decode_ies(create_pdr_ie->value);
    const auto* pdr_id_ie =
        pdr_ies.has_value()
            ? pfcp_core::find_ie(*pdr_ies, static_cast<std::uint16_t>(pfcp_core::IeType::PdrId))
            : nullptr;
    const auto* pdi_ie =
        pdr_ies.has_value()
            ? pfcp_core::find_ie(*pdr_ies, static_cast<std::uint16_t>(pfcp_core::IeType::Pdi))
            : nullptr;
    const auto pdr_id = pdr_id_ie != nullptr ? pfcp_core::decode_pdr_id(pdr_id_ie->value) : std::nullopt;

    // ADR-0050 Stage 2: the real Create URR IE (TS 29.244 §7.5.2.4), if SMF's Stage 1 provisioned
    // one from CHF's actual grant (ADR-0048's Nchf_ConvergedCharging_Create) -- top-level sibling
    // of Create PDR/Create FAR, not nested inside either. Only the fields Annex C.2.1.1's volume-
    // based flow uses are read; URR ID is read from the wire rather than assumed to always be 1,
    // since nothing about UPF's own logic depends on SMF's specific numbering choice.
    const auto* create_urr_ie =
        pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreateUrr));
    std::optional<std::uint32_t> urr_id;
    std::optional<std::uint64_t> urr_volume_threshold;
    std::optional<std::uint64_t> urr_volume_quota;
    if (create_urr_ie != nullptr) {
        const auto urr_ies = pfcp_core::decode_ies(create_urr_ie->value);
        if (urr_ies.has_value()) {
            const auto* urr_id_ie =
                pfcp_core::find_ie(*urr_ies, static_cast<std::uint16_t>(pfcp_core::IeType::UrrId));
            const auto* threshold_ie = pfcp_core::find_ie(
                *urr_ies, static_cast<std::uint16_t>(pfcp_core::IeType::VolumeThreshold));
            const auto* quota_ie =
                pfcp_core::find_ie(*urr_ies, static_cast<std::uint16_t>(pfcp_core::IeType::VolumeQuota));
            if (urr_id_ie != nullptr) {
                urr_id = pfcp_core::decode_urr_id(urr_id_ie->value);
            }
            if (threshold_ie != nullptr) {
                urr_volume_threshold = pfcp_core::decode_volume_total(threshold_ie->value);
            }
            if (quota_ie != nullptr) {
                urr_volume_quota = pfcp_core::decode_volume_total(quota_ie->value);
            }
        }
        if (!urr_id.has_value() || !urr_volume_threshold.has_value() || !urr_volume_quota.has_value()) {
            spdlog::warn("upf: Session Establishment Request carried a malformed Create URR, "
                        "ignoring usage tracking for this session");
        }
    }

    SessionEstablishmentResult result;

    std::vector<std::uint8_t> ies_out;
    pfcp_core::encode_ie(ies_out, static_cast<std::uint16_t>(pfcp_core::IeType::NodeId),
                         pfcp_core::encode_node_id_ipv4(node_ipv4));
    pfcp_core::encode_ie(ies_out, static_cast<std::uint16_t>(pfcp_core::IeType::Cause),
                         pfcp_core::encode_cause(pfcp_core::Cause::RequestAccepted));

    pfcp_core::FSeid up_f_seid;
    up_f_seid.seid = next_seid++;
    up_f_seid.ipv4 = node_ipv4;
    pfcp_core::encode_ie(ies_out, static_cast<std::uint16_t>(pfcp_core::IeType::FSeid),
                         pfcp_core::encode_f_seid_ipv4(up_f_seid));

    if (pdr_id.has_value() && pdi_ie != nullptr) {
        const auto pdi_ies = pfcp_core::decode_ies(pdi_ie->value);
        const auto* f_teid_ie =
            pdi_ies.has_value()
                ? pfcp_core::find_ie(*pdi_ies, static_cast<std::uint16_t>(pfcp_core::IeType::FTeid))
                : nullptr;
        if (f_teid_ie != nullptr && pfcp_core::decode_f_teid_is_choose_request(f_teid_ie->value)) {
            const std::uint32_t allocated_teid = next_teid++;
            std::vector<std::uint8_t> created_pdr;
            pfcp_core::encode_ie(created_pdr, static_cast<std::uint16_t>(pfcp_core::IeType::PdrId),
                                 pfcp_core::encode_pdr_id(*pdr_id));
            pfcp_core::encode_ie(
                created_pdr, static_cast<std::uint16_t>(pfcp_core::IeType::FTeid),
                pfcp_core::encode_f_teid_allocated_ipv4(allocated_teid, node_ipv4));
            pfcp_core::encode_ie(ies_out, static_cast<std::uint16_t>(pfcp_core::IeType::CreatedPdr),
                                 created_pdr);
            spdlog::info("upf: allocated F-TEID {:#x} for PDR ID {}", allocated_teid, *pdr_id);
            // ADR-0043: registers the TEID with the real XDP program so it actually recognizes
            // and decapsulates uplink traffic for this PDR -- a no-op (logged, not fatal) if no
            // datapath was created (e.g. missing privileges, see datapath.hpp's own comment).
            if (datapath != nullptr) {
                datapath->register_teid(allocated_teid);
                if (urr_id.has_value() && urr_volume_threshold.has_value() &&
                    urr_volume_quota.has_value()) {
                    datapath->register_urr(allocated_teid, *urr_volume_threshold, *urr_volume_quota);
                    spdlog::info("upf: registered URR {} for TEID {:#x}: threshold={} quota={} octets",
                                *urr_id, allocated_teid, *urr_volume_threshold, *urr_volume_quota);
                }
            }
            if (urr_id.has_value() && urr_volume_threshold.has_value() && urr_volume_quota.has_value()) {
                result.allocated_teid_with_urr = allocated_teid;
                result.urr_id = urr_id;
            }
        }
    }

    result.ies = std::move(ies_out);
    result.response_header_seid = cp_f_seid->seid;
    return result;
}

// Runs on the main thread (blocking UDP I/O, same "blocking transport gets its own thread"
// discipline ADR-0006/ADR-0030 already established -- here it's simply the only thread, since
// UPF has no HTTP2 server to share time with). Never returns.
void run_pfcp_lifecycle(std::time_t start_time, upf::Datapath* datapath,
                        TeidSessionStore& teid_session_store) {
    boost::asio::io_context ioc;
    boost::asio::ip::udp::socket socket(
        ioc, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), pfcp_core::kPfcpPort));
    spdlog::info("upf: listening for PFCP/N4 (UDP) on 0.0.0.0:{}", pfcp_core::kPfcpPort);

    constexpr std::array<std::uint8_t, 4> kNodeIpv4{127, 0, 0, 1}; // this lab's loopback-only scope
    std::uint64_t next_seid = 1;
    std::uint32_t next_teid = 1;

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
        } else if (header->message_type == pfcp_core::MessageType::SessionEstablishmentRequest) {
            const auto result = build_session_establishment_response_ies(
                ie_bytes, kNodeIpv4, next_seid, next_teid, datapath);
            if (!result.has_value()) {
                spdlog::warn("upf: malformed Session Establishment Request from {}, ignoring",
                            sender.address().to_string());
                continue;
            }
            resp_header.message_type = pfcp_core::MessageType::SessionEstablishmentResponse;
            resp_header.has_seid = true;
            resp_header.seid = result->response_header_seid;
            resp_ies = result->ies;
            spdlog::info("upf: Sx Session established from {}", sender.address().to_string());
            // ADR-0050 Stage 2: `sender` here is SMF's real, persistent PFCP peer endpoint (its
            // PfcpPeer, ADR-0050 Stage 0, sends every request -- including this one -- from the
            // same bound socket it also listens on), so it can be reused verbatim as the address
            // to send a real, unsolicited Sx Session Report Request back to later.
            if (result->allocated_teid_with_urr.has_value() && result->urr_id.has_value()) {
                UrrSessionInfo info;
                info.smf_endpoint = sender;
                info.cp_seid = result->response_header_seid;
                info.urr_id = *result->urr_id;
                teid_session_store.put(*result->allocated_teid_with_urr, info);
            }
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

    // ADR-0050 Stage 2: outlive `datapath` below (declared here, before it, so both are still
    // alive for as long as the datapath's ring-buffer-polling thread might invoke the usage
    // report handler -- which, since this process never terminates, is the process's entire
    // lifetime) and are shared between run_pfcp_lifecycle's main thread (populates
    // teid_session_store) and the datapath's own thread (reads it, sends via report_sender).
    TeidSessionStore teid_session_store;
    ReportSender report_sender;
    // Real PFCP header Sequence Number (TS 29.244 §7.2.2.1) for messages UPF itself originates --
    // a real, node-level counter, deliberately NOT the same value as UR-SEQN (TeidSessionStore's
    // per-URR counter): the two are different real fields with different scopes/lifetimes in the
    // spec, and conflating them was considered and rejected while writing this.
    std::atomic<std::uint32_t> next_pfcp_sequence_number{1};

    auto usage_report_handler = [&teid_session_store, &report_sender, &next_pfcp_sequence_number](
                                     std::uint32_t teid, std::uint64_t total_octets,
                                     bool quota_exhausted) {
        const auto info = teid_session_store.get_and_advance_seqn(teid);
        if (!info.has_value()) {
            spdlog::warn("upf: usage report fired for TEID {:#x} with no known session, dropping",
                        teid);
            return;
        }
        const auto& [session, ur_seqn] = *info;

        // TS 29.244 §7.5.8.3 Usage Report (within Session Report Request): URR ID, UR-SEQN,
        // Usage Report Trigger, Volume Measurement -- the fields Annex C.2.1.1's volume-based
        // flow needs; this project models no others (see session_ies.hpp's own file-header
        // disclosure of that scope).
        std::vector<std::uint8_t> usage_report;
        pfcp_core::encode_ie(usage_report, static_cast<std::uint16_t>(pfcp_core::IeType::UrrId),
                             pfcp_core::encode_urr_id(session.urr_id));
        pfcp_core::encode_ie(usage_report, static_cast<std::uint16_t>(pfcp_core::IeType::UrSeqn),
                             pfcp_core::encode_ur_seqn(ur_seqn));
        pfcp_core::encode_ie(
            usage_report, static_cast<std::uint16_t>(pfcp_core::IeType::UsageReportTrigger),
            quota_exhausted ? pfcp_core::encode_usage_report_trigger_volqu()
                            : pfcp_core::encode_usage_report_trigger_volth());
        pfcp_core::encode_ie(usage_report,
                             static_cast<std::uint16_t>(pfcp_core::IeType::VolumeMeasurement),
                             pfcp_core::encode_volume_total(total_octets));

        std::vector<std::uint8_t> ies;
        pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::ReportType),
                             pfcp_core::encode_report_type_usage_report());
        pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::UsageReport),
                             usage_report);

        pfcp_core::Header header;
        header.message_type = pfcp_core::MessageType::SessionReportRequest;
        // TS 29.244's addressing rule this file already relies on elsewhere (see
        // SessionEstablishmentResult's own comment): the sending entity uses the SEID value
        // provided by the corresponding receiving entity -- here, SMF's own CP F-SEID from this
        // session's Establishment Request.
        header.has_seid = true;
        header.seid = session.cp_seid;
        header.sequence_number = next_pfcp_sequence_number.fetch_add(1);

        auto bytes = pfcp_core::encode_header(header, static_cast<std::uint16_t>(ies.size()));
        bytes.insert(bytes.end(), ies.begin(), ies.end());
        report_sender.send(session.smf_endpoint, bytes);
        spdlog::info("upf: sent Sx Session Report Request to {} for TEID {:#x}: total={} octets, "
                    "trigger={}",
                    session.smf_endpoint.address().to_string(), teid, total_octets,
                    quota_exhausted ? "VOLQU" : "VOLTH");
    };

    // ADR-0043: real eBPF/XDP GTP-U decapsulation datapath. Failure is disclosed and non-fatal --
    // PFCP control-plane signalling (Stages 1-3) works identically with or without it (see
    // datapath.hpp's own comment for why, and what privileges a real datapath needs).
    auto datapath = upf::Datapath::create(usage_report_handler);
    if (!datapath.has_value()) {
        spdlog::warn("upf: eBPF/XDP datapath not started (see preceding error) -- PFCP "
                    "control-plane signalling still works, but no uplink packet will actually "
                    "be decapsulated/forwarded");
    }

    std::thread(run_nrf_lifecycle, upf_instance_id).detach();
    run_pfcp_lifecycle(start_time, datapath.has_value() ? &*datapath : nullptr,
                       teid_session_store); // blocks forever
    return 0;
}
