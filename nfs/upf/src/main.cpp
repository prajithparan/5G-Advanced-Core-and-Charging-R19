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

#include "sbi_core/http2_client.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/oauth2_client.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/sbi_headers.hpp"
#include "sbi_core/uuid.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "TS29122_CommonData_grp.hpp"
#include "datapath.hpp"
#include "pfcp_core/common_ies.hpp"
#include "pfcp_core/header.hpp"
#include "pfcp_core/ie.hpp"
#include "pfcp_core/session_ies.hpp"

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
    std::optional<std::pair<UrrSessionInfo, std::uint32_t>>
    get_and_advance_seqn(std::uint32_t teid) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sessions_.find(teid);
        if (it == sessions_.end()) {
            return std::nullopt;
        }
        const std::uint32_t seqn = next_ur_seqn_[teid]++;
        return std::make_pair(it->second, seqn);
    }

    // ADR-0050 Stage 5: a pure, non-mutating read -- Session Modification Response needs this
    // session's real cp_seid (to address the response correctly, TS 29.244's addressing rule) but
    // must not advance the UR-SEQN counter as a side effect of that lookup.
    std::optional<UrrSessionInfo> get(std::uint32_t teid) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sessions_.find(teid);
        if (it == sessions_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // ADR-0071, gap-closure Tier 1d: real Session Deletion cleanup -- without this, a deleted
    // session's entry would live here for the rest of the process's lifetime (this project's own
    // disclosed "no restart/session-teardown testing yet" gap, from before Session Deletion had
    // any handler at all, no longer applies once this is called from one). No-op if the TEID isn't
    // present (already removed, or never had a URR).
    void remove(std::uint32_t teid) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(teid);
        next_ur_seqn_.erase(teid);
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::uint32_t, UrrSessionInfo> sessions_;
    std::unordered_map<std::uint32_t, std::uint32_t> next_ur_seqn_;
};

// ADR-0050 Stage 5: resolves a real Session Modification Request's header SEID (this UPF's own
// F-SEID for the session, allocated at Establishment -- see SessionEstablishmentResult::up_seid)
// back to the TEID it corresponds to. Written and read on run_pfcp_lifecycle's single thread only
// (unlike TeidSessionStore, no datapath-thread access here) -- mutex-guarded anyway, for the same
// "don't rely on today's single-thread access staying true" reasoning already applied elsewhere in
// this file.
class SeidToTeidStore {
public:
    void put(std::uint64_t seid, std::uint32_t teid) {
        std::lock_guard<std::mutex> lock(mutex_);
        seid_to_teid_[seid] = teid;
    }

    std::optional<std::uint32_t> get(std::uint64_t seid) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = seid_to_teid_.find(seid);
        if (it == seid_to_teid_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // ADR-0071: same real Session Deletion cleanup rationale as TeidSessionStore::remove above.
    void remove(std::uint64_t seid) {
        std::lock_guard<std::mutex> lock(mutex_);
        seid_to_teid_.erase(seid);
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::uint32_t> seid_to_teid_;
};

// ADR-0050 Stage 2: a small, dedicated UDP socket the usage report handler uses (from Datapath's
// own polling thread) to fire-and-forget a Session Report Request to SMF. Deliberately separate
// from run_pfcp_lifecycle's own receive socket -- that one is only ever touched by the main
// thread; giving the datapath thread its own socket avoids needing to reason about concurrent
// send/receive on one boost::asio::ip::udp::socket from two threads at once.
class ReportSender {
public:
    ReportSender() : socket_(ioc_, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)) {}

    void send(const boost::asio::ip::udp::endpoint& target,
              const std::vector<std::uint8_t>& bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(bytes), target, 0, ec);
        if (ec) {
            spdlog::warn("upf: failed to send unsolicited PFCP message to {}: {}",
                         target.address().to_string(),
                         ec.message());
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
    pfcp_core::encode_ie(ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::RecoveryTimeStamp),
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

std::vector<std::uint8_t>
build_association_setup_response_ies(std::time_t start_time,
                                     std::array<std::uint8_t, 4> node_ipv4) {
    std::vector<std::uint8_t> ies;
    pfcp_core::encode_ie(ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::NodeId),
                         pfcp_core::encode_node_id_ipv4(node_ipv4));
    pfcp_core::encode_ie(ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::Cause),
                         pfcp_core::encode_cause(pfcp_core::Cause::RequestAccepted));
    pfcp_core::encode_ie(ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::RecoveryTimeStamp),
                         pfcp_core::encode_recovery_time_stamp(start_time));
    pfcp_core::encode_ie(ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::UpFunctionFeatures),
                         encode_up_function_features_ftup_only());
    return ies;
}

struct SessionEstablishmentResult {
    std::vector<std::uint8_t> ies;
    // The header SEID for this response: TS 29.244's addressing rule ("the sending entity uses
    // the SEID value provided by the corresponding receiving entity") means UPF's response header
    // must carry the value the CP F-SEID IE in the request said to use -- not UPF's own new SEID.
    std::uint64_t response_header_seid = 0;
    // ADR-0071 (gap-closure Tier 1d): set whenever this session allocated a real uplink F-TEID,
    // regardless of whether a URR was also provisioned -- run_pfcp_lifecycle needs this
    // unconditionally to register SeidToTeidStore, since ANY session with an allocated F-TEID must
    // be reachable by a later real Session Modification/Deletion (Update/Remove QER, Update/Remove
    // BAR, Session Deletion), not just ones with a URR. Real bug fixed this turn: earlier code
    // (ADR-0050 Stage 5) only ever set/used allocated_teid_with_urr, so a QER-only session (no
    // charging URR -- an entirely ordinary real case) could establish successfully but then could
    // never be found by SEID for any later Modification/Deletion, a genuine conformance gap this
    // field closes. allocated_teid_with_urr is kept as a SEPARATE field (set only when a URR was
    // ALSO provisioned) since TeidSessionStore's own real purpose -- addressing an unsolicited
    // Session Report Request back to SMF -- genuinely only applies to URR-bearing sessions.
    std::optional<std::uint32_t> allocated_teid;
    std::optional<std::uint32_t> allocated_teid_with_urr;
    std::optional<std::uint32_t> urr_id;
    // ADR-0050 Stage 5: this UPF's own newly-generated F-SEID for the session -- exposed so
    // run_pfcp_lifecycle can register it in SeidToTeidStore (a later real Session Modification
    // Request addresses this session using exactly this value, per the same addressing rule this
    // struct's own response_header_seid comment already documents).
    std::uint64_t up_seid = 0;
};

// Session Establishment (TS 29.244 §7.5.2/§7.5.3, ADR-0042). Decodes the CP F-SEID and the first
// Create PDR/Create FAR pair, allocates a real local F-TEID (if the PDI's F-TEID requested CH)
// and a real UP F-SEID, and builds the response. `next_seid`/`next_teid` are simple counters --
// safe as plain (non-atomic) locals since this function only ever runs on run_pfcp_lifecycle's
// single thread.
std::optional<SessionEstablishmentResult>
build_session_establishment_response_ies(const std::vector<std::uint8_t>& request_ies,
                                         std::array<std::uint8_t, 4> node_ipv4,
                                         std::uint64_t& next_seid,
                                         std::uint32_t& next_teid,
                                         upf::Datapath* datapath) {
    const auto ies = pfcp_core::decode_ies(request_ies);
    if (!ies.has_value()) {
        return std::nullopt;
    }
    const auto* cp_f_seid_ie =
        pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::FSeid));
    const auto* create_pdr_ie =
        pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreatePdr));
    if (cp_f_seid_ie == nullptr || create_pdr_ie == nullptr) {
        spdlog::warn(
            "upf: Session Establishment Request missing mandatory CP F-SEID or Create PDR");
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
    const auto pdr_id =
        pdr_id_ie != nullptr ? pfcp_core::decode_pdr_id(pdr_id_ie->value) : std::nullopt;

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
            const auto* quota_ie = pfcp_core::find_ie(
                *urr_ies, static_cast<std::uint16_t>(pfcp_core::IeType::VolumeQuota));
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
        if (!urr_id.has_value() || !urr_volume_threshold.has_value() ||
            !urr_volume_quota.has_value()) {
            spdlog::warn("upf: Session Establishment Request carried a malformed Create URR, "
                         "ignoring usage tracking for this session");
        }
    }

    // ADR-0071, gap-closure Tier 1d: the real Create QER IE (TS 29.244 Table 7.5.2.5-1), top-level
    // sibling of Create PDR/Create FAR/Create URR, same as the Create URR handling above. Real,
    // disclosed simplification matching Create URR's own established scope: only the first Create
    // QER is applied (this build's per-TEID datapath map holds one QER slot, same "one per
    // session" narrowing already applied to URR). Gate Status is real Mandatory in Create QER; a
    // request missing it is treated as malformed and this session gets no QoS enforcement, logged
    // rather than silently accepted. Maximum Bitrate is real Conditional -- absent means no real
    // MBR enforcement (register_qer's own header comment already documents mbr_ul_kbps=0 as that
    // exact case).
    const auto* create_qer_ie =
        pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreateQer));
    std::optional<pfcp_core::GateStatus> qer_gate_status;
    std::uint32_t qer_mbr_ul_kbps = 0;
    if (create_qer_ie != nullptr) {
        const auto qer_ies = pfcp_core::decode_ies(create_qer_ie->value);
        if (qer_ies.has_value()) {
            const auto* gate_status_ie = pfcp_core::find_ie(
                *qer_ies, static_cast<std::uint16_t>(pfcp_core::IeType::GateStatus));
            const auto* mbr_ie =
                pfcp_core::find_ie(*qer_ies, static_cast<std::uint16_t>(pfcp_core::IeType::Mbr));
            if (gate_status_ie != nullptr) {
                qer_gate_status = pfcp_core::decode_gate_status(gate_status_ie->value);
            }
            if (mbr_ie != nullptr) {
                if (const auto mbr = pfcp_core::decode_mbr(mbr_ie->value); mbr.has_value()) {
                    qer_mbr_ul_kbps = static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(mbr->ul_kbps, 0xFFFFFFFFULL));
                }
            }
        }
        if (!qer_gate_status.has_value()) {
            spdlog::warn("upf: Session Establishment Request carried a malformed/incomplete "
                         "Create QER (missing Mandatory Gate Status), ignoring QoS enforcement "
                         "for this session");
        }
    }

    // ADR-0071: real Create BAR IE (TS 29.244 Table 7.5.2.6-1), parsed and acknowledged only --
    // this project has no downlink datapath (see this file's own header comment: no downlink
    // PDR/FAR exists, since that needs NGAP PDU Session Resource Setup, still not implemented), so
    // a BAR's real purpose (buffering downlink data while paging/notifying the UE) cannot actually
    // be enforced here. Disclosed, deliberate scope: PFCP-level parse/log only, no BAR state is
    // stored or applied to any datapath.
    const auto* create_bar_ie =
        pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreateBar));
    if (create_bar_ie != nullptr) {
        const auto bar_ies = pfcp_core::decode_ies(create_bar_ie->value);
        const auto* bar_id_ie =
            bar_ies.has_value()
                ? pfcp_core::find_ie(*bar_ies, static_cast<std::uint16_t>(pfcp_core::IeType::BarId))
                : nullptr;
        const auto bar_id =
            bar_id_ie != nullptr ? pfcp_core::decode_bar_id(bar_id_ie->value) : std::nullopt;
        if (bar_id.has_value()) {
            spdlog::info("upf: Create BAR {} acknowledged (PFCP-level only -- no downlink "
                         "datapath exists to enforce buffering, see ADR-0071)",
                         *bar_id);
        } else {
            spdlog::warn("upf: Session Establishment Request carried a malformed Create BAR "
                         "(missing Mandatory BAR ID)");
        }
    }

    SessionEstablishmentResult result;

    std::vector<std::uint8_t> ies_out;
    pfcp_core::encode_ie(ies_out,
                         static_cast<std::uint16_t>(pfcp_core::IeType::NodeId),
                         pfcp_core::encode_node_id_ipv4(node_ipv4));
    pfcp_core::encode_ie(ies_out,
                         static_cast<std::uint16_t>(pfcp_core::IeType::Cause),
                         pfcp_core::encode_cause(pfcp_core::Cause::RequestAccepted));

    pfcp_core::FSeid up_f_seid;
    up_f_seid.seid = next_seid++;
    up_f_seid.ipv4 = node_ipv4;
    result.up_seid = up_f_seid.seid;
    pfcp_core::encode_ie(ies_out,
                         static_cast<std::uint16_t>(pfcp_core::IeType::FSeid),
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
            pfcp_core::encode_ie(created_pdr,
                                 static_cast<std::uint16_t>(pfcp_core::IeType::PdrId),
                                 pfcp_core::encode_pdr_id(*pdr_id));
            pfcp_core::encode_ie(
                created_pdr,
                static_cast<std::uint16_t>(pfcp_core::IeType::FTeid),
                pfcp_core::encode_f_teid_allocated_ipv4(allocated_teid, node_ipv4));
            pfcp_core::encode_ie(
                ies_out, static_cast<std::uint16_t>(pfcp_core::IeType::CreatedPdr), created_pdr);
            spdlog::info("upf: allocated F-TEID {:#x} for PDR ID {}", allocated_teid, *pdr_id);
            // ADR-0043: registers the TEID with the real XDP program so it actually recognizes
            // and decapsulates uplink traffic for this PDR -- a no-op (logged, not fatal) if no
            // datapath was created (e.g. missing privileges, see datapath.hpp's own comment).
            if (datapath != nullptr) {
                datapath->register_teid(allocated_teid);
                if (urr_id.has_value() && urr_volume_threshold.has_value() &&
                    urr_volume_quota.has_value()) {
                    datapath->register_urr(
                        allocated_teid, *urr_volume_threshold, *urr_volume_quota);
                    spdlog::info(
                        "upf: registered URR {} for TEID {:#x}: threshold={} quota={} octets",
                        *urr_id,
                        allocated_teid,
                        *urr_volume_threshold,
                        *urr_volume_quota);
                }
                if (qer_gate_status.has_value()) {
                    datapath->register_qer(allocated_teid,
                                           qer_gate_status->ul_closed,
                                           qer_gate_status->dl_closed,
                                           qer_mbr_ul_kbps);
                    spdlog::info("upf: registered QER for TEID {:#x}: ul_gate={} dl_gate={} "
                                 "mbr_ul_kbps={}",
                                 allocated_teid,
                                 qer_gate_status->ul_closed ? "CLOSED" : "OPEN",
                                 qer_gate_status->dl_closed ? "CLOSED" : "OPEN",
                                 qer_mbr_ul_kbps);
                }
            }
            // ADR-0071: unconditional -- see this field's own header comment on the real bug this
            // fixes (SEID resolution must work for every session with an allocated F-TEID).
            result.allocated_teid = allocated_teid;
            if (urr_id.has_value() && urr_volume_threshold.has_value() &&
                urr_volume_quota.has_value()) {
                result.allocated_teid_with_urr = allocated_teid;
                result.urr_id = urr_id;
            }
        }
    }

    result.ies = std::move(ies_out);
    result.response_header_seid = cp_f_seid->seid;
    return result;
}

struct SessionModificationResult {
    std::vector<std::uint8_t> ies;
    std::uint64_t response_header_seid = 0;
};

// Real Sx Session Modification (TS 29.244 §7.5.4/§7.5.5, ADR-0050 Stage 5) -- this build's only
// supported modification is a real Update URR (grouped IE type=13, TS 29.244 Table 7.5.4.4-1)
// pushing a re-authorized Volume Threshold/Volume Quota for an already-created URR. `request_seid`
// is the incoming header's own SEID -- this UPF's own F-SEID for the session (the value the CP
// addressed the request TO, per the addressing rule SessionEstablishmentResult's own comment
// documents), not what the response should echo back.
std::optional<SessionModificationResult>
build_session_modification_response_ies(const std::vector<std::uint8_t>& request_ies,
                                        std::uint64_t request_seid,
                                        SeidToTeidStore& seid_to_teid_store,
                                        TeidSessionStore& teid_session_store,
                                        upf::Datapath* datapath) {
    SessionModificationResult result;
    // Real spec addressing rule: the response echoes the CP's own SEID for this session -- only
    // known via the session's already-stored UrrSessionInfo below. Falls back to request_seid
    // (technically the wrong direction, same disclosed simplification ADR-0050 Stage 0 already
    // carries for Session Report Response) only if that lookup fails.
    result.response_header_seid = request_seid;

    const auto teid = seid_to_teid_store.get(request_seid);
    if (!teid.has_value()) {
        spdlog::warn("upf: Session Modification Request references unknown SEID {:#x}",
                     request_seid);
        pfcp_core::encode_ie(result.ies,
                             static_cast<std::uint16_t>(pfcp_core::IeType::Cause),
                             pfcp_core::encode_cause(pfcp_core::Cause::RequestRejected));
        return result;
    }
    if (const auto session_info = teid_session_store.get(*teid); session_info.has_value()) {
        result.response_header_seid = session_info->cp_seid;
    }

    const auto ies = pfcp_core::decode_ies(request_ies);

    // ADR-0071, gap-closure Tier 1d: real Session Modification can carry any combination of
    // Update URR / Update QER / Remove QER / Update BAR / Remove BAR as top-level sibling IEs (TS
    // 29.244 Table 7.5.4.1-1 -- all Conditional/Optional, none Mandatory). This project's earlier
    // Stage 5 build only ever handled Update URR and returned as soon as that one IE was checked;
    // extended here (rather than kept as separate early-returns) so a single real request touching
    // more than one of these actually gets all of them applied, not just the first. `failed` tracks
    // whether ANY requested modification could not be applied -- the whole response is Rejected if
    // so (this build has no per-IE Failed Rule ID reporting, a real, disclosed simplification: TS
    // 29.244 does support partial success via Failed Rule ID, not implemented here).
    bool failed = false;

    const auto* update_urr_ie =
        ies.has_value()
            ? pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::UpdateUrr))
            : nullptr;
    if (update_urr_ie != nullptr) {
        const auto update_urr_ies = pfcp_core::decode_ies(update_urr_ie->value);
        const auto* threshold_ie =
            update_urr_ies.has_value()
                ? pfcp_core::find_ie(*update_urr_ies,
                                     static_cast<std::uint16_t>(pfcp_core::IeType::VolumeThreshold))
                : nullptr;
        const auto* quota_ie =
            update_urr_ies.has_value()
                ? pfcp_core::find_ie(*update_urr_ies,
                                     static_cast<std::uint16_t>(pfcp_core::IeType::VolumeQuota))
                : nullptr;
        const auto new_threshold = threshold_ie != nullptr
                                       ? pfcp_core::decode_volume_total(threshold_ie->value)
                                       : std::nullopt;
        const auto new_quota =
            quota_ie != nullptr ? pfcp_core::decode_volume_total(quota_ie->value) : std::nullopt;
        if (!new_threshold.has_value() || !new_quota.has_value() || datapath == nullptr ||
            !datapath->update_urr_thresholds(*teid, *new_threshold, *new_quota)) {
            spdlog::warn("upf: failed to apply Update URR for TEID {:#x}", *teid);
            failed = true;
        } else {
            spdlog::info("upf: applied Update URR for TEID {:#x}: threshold={} quota={} octets",
                         *teid,
                         *new_threshold,
                         *new_quota);
        }
    }

    // ADR-0071: real Update QER (TS 29.244 Table 7.5.4.5-1) -- QER ID is Mandatory but, matching
    // this build's own "one QER per TEID" scope already established at Session Establishment, the
    // actual value on the wire is not cross-checked against a stored QER ID; only its presence is
    // required, same disclosed narrowing as URR ID's own handling throughout this file. Gate
    // Status/Maximum Bitrate are real Conditional -- passed through as std::optional so
    // Datapath::update_qer can do a real read-modify-write (see its own header comment for why a
    // naive full-overwrite would be a correctness bug here).
    const auto* update_qer_ie =
        ies.has_value()
            ? pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::UpdateQer))
            : nullptr;
    if (update_qer_ie != nullptr) {
        const auto update_qer_ies = pfcp_core::decode_ies(update_qer_ie->value);
        const auto* qer_id_ie =
            update_qer_ies.has_value()
                ? pfcp_core::find_ie(*update_qer_ies,
                                     static_cast<std::uint16_t>(pfcp_core::IeType::QerId))
                : nullptr;
        const auto* gate_status_ie =
            update_qer_ies.has_value()
                ? pfcp_core::find_ie(*update_qer_ies,
                                     static_cast<std::uint16_t>(pfcp_core::IeType::GateStatus))
                : nullptr;
        const auto* mbr_ie =
            update_qer_ies.has_value()
                ? pfcp_core::find_ie(*update_qer_ies,
                                     static_cast<std::uint16_t>(pfcp_core::IeType::Mbr))
                : nullptr;
        std::optional<bool> new_ul_gate_closed;
        std::optional<bool> new_dl_gate_closed;
        if (gate_status_ie != nullptr) {
            if (const auto gate = pfcp_core::decode_gate_status(gate_status_ie->value);
                gate.has_value()) {
                new_ul_gate_closed = gate->ul_closed;
                new_dl_gate_closed = gate->dl_closed;
            }
        }
        std::optional<std::uint32_t> new_mbr_ul_kbps;
        if (mbr_ie != nullptr) {
            if (const auto mbr = pfcp_core::decode_mbr(mbr_ie->value); mbr.has_value()) {
                new_mbr_ul_kbps = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(mbr->ul_kbps, 0xFFFFFFFFULL));
            }
        }
        if (qer_id_ie == nullptr || datapath == nullptr ||
            !datapath->update_qer(*teid, new_ul_gate_closed, new_dl_gate_closed, new_mbr_ul_kbps)) {
            spdlog::warn("upf: failed to apply Update QER for TEID {:#x}", *teid);
            failed = true;
        } else {
            spdlog::info("upf: applied Update QER for TEID {:#x}", *teid);
        }
    }

    // ADR-0071: real Remove QER (TS 29.244 Table 7.5.4.9-1). Idempotent by design: if no QER is
    // currently registered (already removed, or never created), the real intent -- "no QER active
    // for this TEID" -- is already true, so this does NOT set `failed`, only logs.
    const auto* remove_qer_ie =
        ies.has_value()
            ? pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::RemoveQer))
            : nullptr;
    if (remove_qer_ie != nullptr) {
        if (datapath != nullptr && datapath->remove_qer(*teid)) {
            spdlog::info("upf: removed QER for TEID {:#x}", *teid);
        } else {
            spdlog::warn("upf: Remove QER for TEID {:#x} found no QER to remove", *teid);
        }
    }

    // ADR-0071: real Update BAR / Remove BAR (TS 29.244 Table 7.5.4.11-1/7.5.4.12-1) -- same
    // disclosed "parse/log only, no downlink datapath to apply it to" scope as Create BAR's own
    // handling in build_session_establishment_response_ies. Always a no-op success: there is no
    // stored BAR state this build could fail to find.
    const auto* update_bar_ie =
        ies.has_value()
            ? pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::UpdateBar))
            : nullptr;
    if (update_bar_ie != nullptr) {
        spdlog::info("upf: Update BAR for TEID {:#x} acknowledged (PFCP-level only, see ADR-0071)",
                     *teid);
    }
    const auto* remove_bar_ie =
        ies.has_value()
            ? pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::RemoveBar))
            : nullptr;
    if (remove_bar_ie != nullptr) {
        spdlog::info("upf: Remove BAR for TEID {:#x} acknowledged (PFCP-level only, see ADR-0071)",
                     *teid);
    }

    pfcp_core::encode_ie(result.ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::Cause),
                         pfcp_core::encode_cause(failed ? pfcp_core::Cause::RequestRejected
                                                        : pfcp_core::Cause::RequestAccepted));
    return result;
}

struct SessionDeletionResult {
    std::vector<std::uint8_t> ies;
    std::uint64_t response_header_seid = 0;
};

// ADR-0071, gap-closure Tier 1d: real Sx Session Deletion (TS 29.244 §7.5.6/§7.5.7) -- the one
// PFCP message type this build had no handler for at all before this turn (previously fell into
// run_pfcp_lifecycle's catch-all "no handler yet, ignoring" branch, never responding).
// `request_seid` is the incoming header's own SEID, same UPF-addressed-by-CP meaning as
// build_session_modification_response_ies's own parameter of the same name.
std::optional<SessionDeletionResult>
build_session_deletion_response_ies(std::uint64_t request_seid,
                                    SeidToTeidStore& seid_to_teid_store,
                                    TeidSessionStore& teid_session_store,
                                    upf::Datapath* datapath) {
    SessionDeletionResult result;
    result.response_header_seid = request_seid;

    const auto teid = seid_to_teid_store.get(request_seid);
    if (!teid.has_value()) {
        // Real spec Table 8.2.1-1 (Cause): "Session context not found... if the F-SEID included in
        // a Sx Session Modification/Deletion Request message is unknown" -- the exact real case
        // here, not the generic RequestRejected earlier PFCP work in this file used before this
        // Cause value existed.
        spdlog::warn("upf: Session Deletion Request references unknown SEID {:#x}", request_seid);
        pfcp_core::encode_ie(result.ies,
                             static_cast<std::uint16_t>(pfcp_core::IeType::Cause),
                             pfcp_core::encode_cause(pfcp_core::Cause::SessionContextNotFound));
        return result;
    }

    // Real spec addressing rule (same as Session Modification's own use of this pattern): the
    // response echoes the CP's own SEID for this session, known from the already-stored
    // UrrSessionInfo if this session ever provisioned a URR.
    const auto session_info = teid_session_store.get(*teid);
    if (session_info.has_value()) {
        result.response_header_seid = session_info->cp_seid;
    }

    // ADR-0071: real cumulative usage, if any URR was provisioned for this session -- this also
    // tears down ALL per-TEID datapath state (teid_map/urr_map/qer_map), the real, full session
    // teardown TS 29.244 §7.5.6 requires at Sx session termination. A missing/absent datapath
    // (e.g. no eBPF privileges, see datapath.hpp's own comment) just means no real total to
    // report -- PFCP-level deletion still succeeds, same "control-plane works with or without a
    // datapath" pattern this file already follows elsewhere.
    const std::optional<std::uint64_t> total_octets =
        datapath != nullptr ? datapath->remove_teid(*teid) : std::nullopt;

    if (total_octets.has_value() && session_info.has_value()) {
        const auto report_info = teid_session_store.get_and_advance_seqn(*teid);
        // TS 29.244 Table 7.5.7.2-1's own narrower field set (URR ID, UR-SEQN, Usage Report
        // Trigger, Volume Measurement) -- see ie.hpp's own UsageReportSessionDeletion comment for
        // why this is real IE type 79, not the 80 used in the unsolicited Session Report path
        // above.
        std::vector<std::uint8_t> usage_report;
        pfcp_core::encode_ie(usage_report,
                             static_cast<std::uint16_t>(pfcp_core::IeType::UrrId),
                             pfcp_core::encode_urr_id(session_info->urr_id));
        pfcp_core::encode_ie(
            usage_report,
            static_cast<std::uint16_t>(pfcp_core::IeType::UrSeqn),
            pfcp_core::encode_ur_seqn(report_info.has_value() ? report_info->second : 0));
        pfcp_core::encode_ie(usage_report,
                             static_cast<std::uint16_t>(pfcp_core::IeType::UsageReportTrigger),
                             pfcp_core::encode_usage_report_trigger_termr());
        pfcp_core::encode_ie(usage_report,
                             static_cast<std::uint16_t>(pfcp_core::IeType::VolumeMeasurement),
                             pfcp_core::encode_volume_total(*total_octets));
        pfcp_core::encode_ie(
            result.ies,
            static_cast<std::uint16_t>(pfcp_core::IeType::UsageReportSessionDeletion),
            usage_report);
        spdlog::info("upf: Session Deletion for TEID {:#x} reporting final usage: {} octets",
                     *teid,
                     *total_octets);
    }

    teid_session_store.remove(*teid);
    seid_to_teid_store.remove(request_seid);

    pfcp_core::encode_ie(result.ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::Cause),
                         pfcp_core::encode_cause(pfcp_core::Cause::RequestAccepted));
    spdlog::info("upf: Sx Session deleted for TEID {:#x}", *teid);
    return result;
}

// Runs on the main thread (blocking UDP I/O, same "blocking transport gets its own thread"
// discipline ADR-0006/ADR-0030 already established -- here it's simply the only thread, since
// UPF has no HTTP2 server to share time with). Never returns.
void run_pfcp_lifecycle(std::time_t start_time,
                        upf::Datapath* datapath,
                        TeidSessionStore& teid_session_store,
                        SeidToTeidStore& seid_to_teid_store) {
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
            spdlog::info("upf: replying to Heartbeat Request from {}",
                         sender.address().to_string());
        } else if (header->message_type == pfcp_core::MessageType::AssociationSetupRequest) {
            resp_header.message_type = pfcp_core::MessageType::AssociationSetupResponse;
            resp_ies = build_association_setup_response_ies(start_time, kNodeIpv4);
            spdlog::info("upf: Sx Association Setup accepted from {}",
                         sender.address().to_string());
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
            // ADR-0071: unconditional -- see SessionEstablishmentResult::allocated_teid's own
            // header comment for the real bug this fixes (a QER-only session with no URR used to
            // be unreachable by SEID for any later Modification/Deletion at all).
            if (result->allocated_teid.has_value()) {
                seid_to_teid_store.put(result->up_seid, *result->allocated_teid);
            }
        } else if (header->message_type == pfcp_core::MessageType::SessionModificationRequest) {
            const auto result = build_session_modification_response_ies(
                ie_bytes, header->seid, seid_to_teid_store, teid_session_store, datapath);
            if (!result.has_value()) {
                spdlog::warn("upf: malformed Session Modification Request from {}, ignoring",
                             sender.address().to_string());
                continue;
            }
            resp_header.message_type = pfcp_core::MessageType::SessionModificationResponse;
            resp_header.has_seid = true;
            resp_header.seid = result->response_header_seid;
            resp_ies = result->ies;
            spdlog::info("upf: Sx Session Modification processed from {}",
                         sender.address().to_string());
        } else if (header->message_type == pfcp_core::MessageType::SessionDeletionRequest) {
            const auto result = build_session_deletion_response_ies(
                header->seid, seid_to_teid_store, teid_session_store, datapath);
            if (!result.has_value()) {
                spdlog::warn("upf: malformed Session Deletion Request from {}, ignoring",
                             sender.address().to_string());
                continue;
            }
            resp_header.message_type = pfcp_core::MessageType::SessionDeletionResponse;
            resp_header.has_seid = true;
            resp_header.seid = result->response_header_seid;
            resp_ies = result->ies;
            spdlog::info("upf: Sx Session Deletion processed from {}",
                         sender.address().to_string());
        } else {
            spdlog::warn("upf: received PFCP message type {} with no handler yet, ignoring",
                         static_cast<int>(header->message_type));
            continue;
        }

        auto resp_bytes =
            pfcp_core::encode_header(resp_header, static_cast<std::uint16_t>(resp_ies.size()));
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
    // ADR-0050 Stage 5: only ever touched by run_pfcp_lifecycle's own single thread (Establishment
    // writes, Modification reads), declared here alongside teid_session_store for the same
    // "outlives everything that could touch it" reasoning, not because it's actually shared with
    // the datapath thread the way teid_session_store is.
    SeidToTeidStore seid_to_teid_store;
    ReportSender report_sender;
    // Real PFCP header Sequence Number (TS 29.244 §7.2.2.1) for messages UPF itself originates --
    // a real, node-level counter, deliberately NOT the same value as UR-SEQN (TeidSessionStore's
    // per-URR counter): the two are different real fields with different scopes/lifetimes in the
    // spec, and conflating them was considered and rejected while writing this.
    std::atomic<std::uint32_t> next_pfcp_sequence_number{1};

    auto usage_report_handler = [&teid_session_store, &report_sender, &next_pfcp_sequence_number](
                                    std::uint32_t teid,
                                    std::uint64_t total_octets,
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
        pfcp_core::encode_ie(usage_report,
                             static_cast<std::uint16_t>(pfcp_core::IeType::UrrId),
                             pfcp_core::encode_urr_id(session.urr_id));
        pfcp_core::encode_ie(usage_report,
                             static_cast<std::uint16_t>(pfcp_core::IeType::UrSeqn),
                             pfcp_core::encode_ur_seqn(ur_seqn));
        pfcp_core::encode_ie(usage_report,
                             static_cast<std::uint16_t>(pfcp_core::IeType::UsageReportTrigger),
                             quota_exhausted ? pfcp_core::encode_usage_report_trigger_volqu()
                                             : pfcp_core::encode_usage_report_trigger_volth());
        pfcp_core::encode_ie(usage_report,
                             static_cast<std::uint16_t>(pfcp_core::IeType::VolumeMeasurement),
                             pfcp_core::encode_volume_total(total_octets));

        std::vector<std::uint8_t> ies;
        pfcp_core::encode_ie(ies,
                             static_cast<std::uint16_t>(pfcp_core::IeType::ReportType),
                             pfcp_core::encode_report_type_usage_report());
        pfcp_core::encode_ie(
            ies, static_cast<std::uint16_t>(pfcp_core::IeType::UsageReport), usage_report);

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
                     session.smf_endpoint.address().to_string(),
                     teid,
                     total_octets,
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
    run_pfcp_lifecycle(start_time,
                       datapath.has_value() ? &*datapath : nullptr,
                       teid_session_store,
                       seid_to_teid_store); // blocks forever
    return 0;
}
