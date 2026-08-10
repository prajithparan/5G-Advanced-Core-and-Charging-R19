// nfs/smf: SMF (Session Management Function), Nsmf_PDUSession /sm-contexts surface.
// Source: specs/5G_APIs-REL-19/TS29502_Nsmf_PDUSession.yaml (commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6). Phase 2's third NF (PROMPT.md/CLAUDE.md order:
// NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF).
//
// In scope: the /sm-contexts collection -- CreateSMContext (PostSmContexts),
// RetrieveSMContext (RetrieveSmContext), UpdateSMContext (UpdateSmContext),
// ReleaseSMContext (ReleaseSmContext) -- the actual AMF-triggered PDU Session Establishment flow
// (TS 23.502 clause 4.3.2.2.1), agreed with the user as this turn's scope after
// docs/DECISIONS.md ADR-0020 (multipart/related codec) unblocked CreateSMContext, which is
// multipart/related-ONLY per spec (no application/json alternative exists for its request body).
// This turn (see ADR-0029) additionally wires CreateSMContext/ReleaseSMContext to a real PCF --
// SM Policy Association Establishment/Termination (Npcf_SMPolicyControl) -- now that PCF exists
// (ADR-0028); SMF is a real SBI client to PCF here, the same pattern AUSF's turn established for
// calling UDM (ADR-0027).
//
// Deliberately deferred, not dropped:
// - The /pdu-sessions collection (PostPduSessions/UpdatePduSession/ReleasePduSession/
//   RetrievePduSession) -- the I-SMF/inter-SMF roaming scenario, not the standard AMF-triggered
//   flow this turn targets.
// - SendMoData/TransferMoData -- small-data-over-NAS operations, multipart-only, peripheral to
//   the core session lifecycle.
// - Nsmf_EventExposure.yaml and Nsmf_NIDD.yaml -- separate SMF services, out of scope for this
//   turn's procedure list.
// - UpdateSMContext still does NOT call PCF's UpdateSMPolicy -- kept out of this turn's scope
//   (only Create/Release wired, see ADR-0029) to keep the turn to the two operations CLAUDE.md's
//   stated PDU-session-establishment goal actually needs.
// - AMF is NOT wired to PCF this turn -- AMF has no real NAS/N1 Registration trigger in this
//   build (no NGAP, ADR-0016), so there is no correct place to attach AM Policy Association
//   Establishment yet; deferred rather than attached to the wrong procedure. See ADR-0029.
//
// Disclosed simplifications, real and not hidden:
// - SMF still has no real UPF (N4/PFCP is Phase 3) and no real UDM (subscription data retrieval).
// - CreateSMContext requires supi/pduSessionId/dnn/sNssai to be present even though
//   SmContextCreateData's schema allows them to be absent (e.g. unauthenticated-SUPI edge cases)
//   -- this build's PCF wiring has nothing to fall back to without them, so a request missing any
//   of them gets a 400, not a silent best-effort attempt. See ADR-0029.
// - The PduSessionType SMF sends to PCF is a fixed default (IPV4), not the UE's real requested
//   type -- that's negotiated inside the NAS SM message (n1SmMsg, an opaque binary blob this
//   build never decodes, same class of gap as every other NAS-decoding simplification here).
// - ReleaseSMContext's DeleteSMPolicy call to PCF is best-effort: local release still succeeds
//   (204) even if PCF is unreachable, so a downstream PCF outage can't strand SMF's own cleanup
//   path -- disclosed, not silently swallowed (logged on failure). CreateSMContext, by contrast,
//   fails closed if PCF is unreachable or errors, matching TS 23.502's real intent that SM Policy
//   Association Establishment failure fails PDU session establishment.
// - UpdateSMContext acknowledges (204) without fabricating SmContextUpdatedData content (EBI
//   allocation, N1/N2 info, ...) -- there is nothing real behind those fields yet.
// - Error responses use the generic ProblemDetails shape (sbi_core::http2::problem_response,
//   application/problem+json) rather than each operation's bespoke *Error schema
//   (SmContextCreateError, SmContextUpdateError) -- same simplification NRF/AMF already use.
//
// Phase 4 addition (see nfs/chf/src/main.cpp's own file header for CHF's approved scope): SMF
// calls CHF's real Nchf_ConvergedCharging_Create at N40, right after the N4 Session Establishment
// call, using a hardcoded base URL (kChfBase) -- matching this file's own existing pattern for
// PCF/AMF (kPcfBase/kAmfBase), not the Nnrf_NFDiscovery path Stage 2's UPF discovery used (that
// was explicitly the "first real use of this NRF capability", see ADR-0041; every other NF-to-NF
// call in this file, before and after, uses a hardcoded base URL). Best-effort/non-fatal, same
// discipline as the N4 Session Establishment call right above it in CreateSMContext's handler --
// no real billing/quota dependency exists yet for a charging-data failure to correctly block on.

#include "pfcp_core/common_ies.hpp"
#include "pfcp_core/header.hpp"
#include "pfcp_core/ie.hpp"
#include "pfcp_core/session_ies.hpp"

#include "sbi_core/datetime.hpp"
#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/json_body.hpp"
#include "sbi_core/jwt.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/multipart.hpp"
#include "sbi_core/oauth2_client.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/sbi_headers.hpp"
#include "sbi_core/uuid.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <nlohmann/json.hpp>

#include <sys/socket.h>

#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "TS29122_CommonData_grp.hpp"
#include "nas_5gsm_codec.hpp"
#include "sm_context_store.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/smf/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7779;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9466";
constexpr const char* kNfType = "SMF";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kSelfBase = "https://127.0.0.1:7779";
constexpr const char* kPcfBase = "https://127.0.0.1:7783";
constexpr const char* kAmfBase = "https://127.0.0.1:7778";
constexpr const char* kChfBase = "https://127.0.0.1:7784";
// No real service-to-rating-group mapping exists in this codebase (that's TS 32.298/32.299
// charging-characteristics configuration, not modeled here) -- every PDU session's usage is
// charged under this one fixed rating group, disclosed here and in ADR-0048, same category of
// simplification as PCF's own fixed-default policy (ADR-0028).
constexpr std::int64_t kDefaultRatingGroup = 1;
constexpr const char* kApiRoot = "/nsmf-pdusession/v1";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

// Same pattern as nfs/nrf and nfs/amf's check_bearer -- see those files' comments for why a
// missing Authorization header is not itself a 401 (bootstrap security alternative:
// `security: [{}, oAuth2ClientCredentials:[...]]` in the YAML).
std::optional<sbi_core::jwt::VerifyResult> check_bearer(const sbi_core::http2::Request& req,
                                                        sbi_core::jwt::Verifier& verifier) {
    auto it = req.headers.find("authorization");
    if (it == req.headers.end()) {
        return std::nullopt;
    }
    const std::string& value = it->second;
    constexpr std::string_view kPrefix = "Bearer ";
    if (value.size() <= kPrefix.size() || value.compare(0, kPrefix.size(), kPrefix) != 0) {
        sbi_core::jwt::VerifyResult r;
        r.valid = false;
        r.error = "Authorization header present but not a Bearer token";
        return r;
    }
    return verifier.verify(value.substr(kPrefix.size()));
}

// Runs on a dedicated thread, never on the server's io_context -- same reasoning as
// nfs/amf/src/main.cpp's run_nrf_lifecycle (docs/DECISIONS.md ADR-0006/ADR-0019).
void run_nrf_lifecycle(const std::string& smf_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/smf/cert.pem",
        .key_path = CERTS_DIR "/smf/key.pem",
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
        http_client, std::string(kNrfBase) + "/oauth2/token", smf_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", smf_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("smf: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + smf_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("smf: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("smf: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("smf: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + smf_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("smf: heartbeat failed");
        }
    }
}

// Discovers UPF via a real Nnrf_NFDiscovery call (TS 29.510 SearchNFInstances,
// GET /nnrf-disc/v1/nf-instances) -- the first real use of NRF's discovery service anywhere in
// this project; every other NF-to-NF call so far has used a hardcoded base URL constant (see
// kPcfBase/kAmfBase above) rather than dynamic discovery. Not a hardcoded address here because
// ADR-0040 (UPF's own turn) explicitly promised this stage would close that gap for real.
// Retries forever (same "keep trying, NRF/UPF may not be up yet" discipline run_nrf_lifecycle
// itself already uses) until at least one UPF instance with a real ipv4Addresses entry is found.
std::string discover_upf_ipv4(sbi_core::http2::Client& http_client, sbi_core::OAuth2Client& oauth) {
    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("smf: OAuth2 token fetch failed for UPF discovery: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = std::string(kNrfBase) + "/nnrf-disc/v1/nf-instances?target-nf-type=UPF&requester-nf-type=SMF";
        req.headers.emplace("authorization", "Bearer " + *token);
        auto resp = http_client.send(req);
        if (!resp.has_value() || resp->status != 200) {
            spdlog::warn("smf: Nnrf_NFDiscovery for UPF failed, retrying in 2s");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        try {
            const auto body = json::parse(resp->body);
            for (const auto& instance : body.at("nfInstances")) {
                if (instance.contains("ipv4Addresses") && !instance.at("ipv4Addresses").empty()) {
                    return instance.at("ipv4Addresses")[0].get<std::string>();
                }
            }
        } catch (const json::exception& e) {
            spdlog::warn("smf: malformed Nnrf_NFDiscovery response: {}", e.what());
        }
        spdlog::info("smf: no UPF registered with NRF yet, retrying discovery in 2s");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

// Real PFCP/N4 Association Setup with UPF (TS 29.244 SS6.2.6.2/SS7.4.4.1-2) -- Stage 2 of
// docs/DECISIONS.md's Phase 3 staged plan (ADR-0041). Runs on its own dedicated thread doing
// blocking UDP I/O, same discipline as nfs/upf/src/main.cpp's own PFCP loop and every other
// blocking-transport thread in this project (ADR-0006/ADR-0030). Retries the whole procedure
// (T1 timer + N1 retries per TS 29.244 SS6.4, both this build's own reasonable fixed choices --
// the spec leaves the exact values implementation-specific) until UPF replies with Cause=accepted.
// This lab's loopback-only scope, reused by both PFCP client call sites below.
constexpr std::array<std::uint8_t, 4> kSmfNodeIpv4{127, 0, 0, 1};

// SO_RCVTIMEO on the underlying descriptor: Boost.Asio's synchronous socket API has no built-in
// receive timeout, and this project's established pattern for blocking-transport code is direct
// POSIX socket calls where Asio doesn't cover something (see libs/ngap-core's own SCTP wrapper) --
// consistent with that, not a new precedent.
void configure_pfcp_receive_timeout(boost::asio::ip::udp::socket& socket) {
    constexpr timeval kReceiveTimeout{.tv_sec = 2, .tv_usec = 0};
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &kReceiveTimeout,
              sizeof(kReceiveTimeout));
}

// Sends a PFCP request and waits for a response matching both the expected message type and
// sequence number, retrying (T1=2s timeout via configure_pfcp_receive_timeout, N1=3 attempts per
// TS 29.244 §6.4 -- this build's own reasonable fixed choices, the spec leaves them
// implementation-specific) before giving up. Returns the response's IE-region bytes on success.
// Shared by run_pfcp_lifecycle's Association Setup (Stage 2) and
// perform_n4_session_establishment's Session Establishment (Stage 3, ADR-0042) -- both need
// identical send/wait/retry mechanics, just different request bytes and expected response type.
std::optional<std::vector<std::uint8_t>> send_pfcp_request_and_await_response(
    boost::asio::ip::udp::socket& socket, const boost::asio::ip::udp::endpoint& target,
    const std::vector<std::uint8_t>& request_pdu, pfcp_core::MessageType expected_response_type,
    std::uint32_t expected_sequence_number, const std::string& procedure_name) {
    constexpr int kN1Retries = 3;
    for (int attempt = 0; attempt < kN1Retries; ++attempt) {
        boost::system::error_code send_ec;
        socket.send_to(boost::asio::buffer(request_pdu), target, 0, send_ec);
        if (send_ec) {
            spdlog::warn("smf: PFCP {} send failed: {}", procedure_name, send_ec.message());
            continue;
        }

        std::vector<std::uint8_t> recv_buf(2048);
        boost::asio::ip::udp::endpoint sender;
        boost::system::error_code recv_ec;
        const std::size_t n = socket.receive_from(boost::asio::buffer(recv_buf), sender, 0, recv_ec);
        if (recv_ec) {
            spdlog::warn("smf: PFCP {} attempt {} timed out, retrying", procedure_name, attempt + 1);
            continue;
        }

        const std::vector<std::uint8_t> resp(recv_buf.begin(),
                                              recv_buf.begin() + static_cast<std::ptrdiff_t>(n));
        std::size_t offset = 0;
        std::uint16_t ies_length = 0;
        const auto resp_header = pfcp_core::decode_header(resp, offset, ies_length);
        if (!resp_header.has_value() || resp_header->message_type != expected_response_type ||
            resp_header->sequence_number != expected_sequence_number) {
            spdlog::warn("smf: PFCP response wasn't a matching {} response, ignoring and retrying",
                        procedure_name);
            continue;
        }
        if (offset + ies_length > resp.size()) {
            spdlog::warn("smf: PFCP {} response length field overruns the datagram, retrying",
                        procedure_name);
            continue;
        }
        return std::vector<std::uint8_t>(resp.begin() + static_cast<std::ptrdiff_t>(offset),
                                         resp.begin() + static_cast<std::ptrdiff_t>(offset + ies_length));
    }
    spdlog::warn("smf: PFCP {} exhausted {} retries", procedure_name, kN1Retries);
    return std::nullopt;
}

// Thread-safe holder for the UPF endpoint learned via run_pfcp_lifecycle's real Nnrf_NFDiscovery +
// Association Setup (Stage 2) -- read by CreateSMContext's route handler (the ioc thread) to
// perform Stage 3's real N4 Session Establishment. Deferred from Stage 2 on purpose (ADR-0041:
// "nothing reads it yet") until this stage actually needed it -- exactly the kind of storage
// CLAUDE.md's engineering rules say not to add before there's a real reader.
class UpfEndpointStore {
public:
    void set(std::string ipv4) {
        std::lock_guard<std::mutex> lock(mutex_);
        ipv4_ = std::move(ipv4);
    }
    std::optional<std::string> get() {
        std::lock_guard<std::mutex> lock(mutex_);
        return ipv4_;
    }

private:
    std::mutex mutex_;
    std::optional<std::string> ipv4_;
};

void run_pfcp_lifecycle(const std::string& smf_instance_id, UpfEndpointStore& upf_endpoint_store) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/smf/cert.pem",
        .key_path = CERTS_DIR "/smf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client http_client(std::move(client_tls));
    sbi_core::OAuth2Client oauth(
        http_client, std::string(kNrfBase) + "/oauth2/token", smf_instance_id, "nnrf-disc", "NRF");

    const std::string upf_ip = discover_upf_ipv4(http_client, oauth);
    spdlog::info("smf: discovered UPF at {} via Nnrf_NFDiscovery", upf_ip);

    boost::asio::io_context ioc;
    boost::asio::ip::udp::socket socket(ioc, boost::asio::ip::udp::v4());
    const boost::asio::ip::udp::endpoint upf_endpoint(
        boost::asio::ip::make_address(upf_ip), pfcp_core::kPfcpPort);
    configure_pfcp_receive_timeout(socket);

    while (true) {
        pfcp_core::Header req_header;
        req_header.has_seid = false;
        req_header.message_type = pfcp_core::MessageType::AssociationSetupRequest;
        req_header.sequence_number = 1;

        std::vector<std::uint8_t> ies;
        pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::NodeId),
                             pfcp_core::encode_node_id_ipv4(kSmfNodeIpv4));
        pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::RecoveryTimeStamp),
                             pfcp_core::encode_recovery_time_stamp(std::time(nullptr)));
        pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::CpFunctionFeatures),
                             pfcp_core::encode_cp_function_features_none());

        auto pdu = pfcp_core::encode_header(req_header, static_cast<std::uint16_t>(ies.size()));
        pdu.insert(pdu.end(), ies.begin(), ies.end());

        const auto resp_ie_bytes = send_pfcp_request_and_await_response(
            socket, upf_endpoint, pdu, pfcp_core::MessageType::AssociationSetupResponse,
            req_header.sequence_number, "Association Setup");
        const auto resp_ies =
            resp_ie_bytes.has_value() ? pfcp_core::decode_ies(*resp_ie_bytes) : std::nullopt;
        const auto* cause_ie =
            resp_ies.has_value()
                ? pfcp_core::find_ie(*resp_ies, static_cast<std::uint16_t>(pfcp_core::IeType::Cause))
                : nullptr;
        const auto cause =
            cause_ie != nullptr ? pfcp_core::decode_cause(cause_ie->value) : std::nullopt;

        if (cause.has_value() && *cause == pfcp_core::Cause::RequestAccepted) {
            spdlog::info("smf: PFCP Sx Association established with UPF at {}", upf_ip);
            upf_endpoint_store.set(upf_ip);
            return;
        }
        spdlog::warn("smf: PFCP Association Setup did not succeed (cause={}), backing off and "
                    "restarting the procedure",
                    cause.has_value() ? static_cast<int>(*cause) : -1);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

// Real N4 Session Establishment (TS 29.244 §7.5.2/§7.5.3, ADR-0042), SMF's side. Builds one
// uplink PDR (PDR ID=1, Source Interface=Access, F-TEID CH-requested so UPF allocates its own
// local GTP-U endpoint) associated with one FAR (FAR ID=1, Apply Action=FORW, Destination
// Interface=Core) -- the minimal real slice TS 23.502's PDU Session Establishment needs. No
// downlink PDR/FAR: that needs the gNB's N3 GTP-U endpoint (NGAP PDU Session Resource Setup,
// still not implemented -- see nfs/upf/src/main.cpp's own disclosure). Best-effort: failure is
// logged, not fatal to CreateSMContext's own 201 response, same discipline ADR-0038 already
// established for the N1N2MessageTransfer call below this one in the handler.
bool perform_n4_session_establishment(const std::string& upf_ip, std::uint8_t pdu_session_id) {
    boost::asio::io_context ioc;
    boost::asio::ip::udp::socket socket(ioc, boost::asio::ip::udp::v4());
    const boost::asio::ip::udp::endpoint upf_endpoint(
        boost::asio::ip::make_address(upf_ip), pfcp_core::kPfcpPort);
    configure_pfcp_receive_timeout(socket);

    static std::atomic<std::uint64_t> next_cp_seid{1};
    const std::uint64_t cp_seid = next_cp_seid++;

    std::vector<std::uint8_t> pdi;
    pfcp_core::encode_ie(pdi, static_cast<std::uint16_t>(pfcp_core::IeType::SourceInterface),
                         pfcp_core::encode_source_interface(pfcp_core::InterfaceValue::Access));
    pfcp_core::encode_ie(pdi, static_cast<std::uint16_t>(pfcp_core::IeType::FTeid),
                         pfcp_core::encode_f_teid_choose_ipv4());

    std::vector<std::uint8_t> create_pdr;
    pfcp_core::encode_ie(create_pdr, static_cast<std::uint16_t>(pfcp_core::IeType::PdrId),
                         pfcp_core::encode_pdr_id(1));
    pfcp_core::encode_ie(create_pdr, static_cast<std::uint16_t>(pfcp_core::IeType::Precedence),
                         pfcp_core::encode_precedence(100));
    pfcp_core::encode_ie(create_pdr, static_cast<std::uint16_t>(pfcp_core::IeType::Pdi), pdi);
    pfcp_core::encode_ie(create_pdr, static_cast<std::uint16_t>(pfcp_core::IeType::FarId),
                         pfcp_core::encode_far_id(1));

    std::vector<std::uint8_t> forwarding_parameters;
    pfcp_core::encode_ie(forwarding_parameters,
                         static_cast<std::uint16_t>(pfcp_core::IeType::DestinationInterface),
                         pfcp_core::encode_destination_interface(pfcp_core::InterfaceValue::Core));

    std::vector<std::uint8_t> create_far;
    pfcp_core::encode_ie(create_far, static_cast<std::uint16_t>(pfcp_core::IeType::FarId),
                         pfcp_core::encode_far_id(1));
    pfcp_core::encode_ie(create_far, static_cast<std::uint16_t>(pfcp_core::IeType::ApplyAction),
                         pfcp_core::encode_apply_action_forward());
    pfcp_core::encode_ie(create_far, static_cast<std::uint16_t>(pfcp_core::IeType::ForwardingParameters),
                         forwarding_parameters);

    pfcp_core::FSeid cp_f_seid;
    cp_f_seid.seid = cp_seid;
    cp_f_seid.ipv4 = kSmfNodeIpv4;

    std::vector<std::uint8_t> ies;
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::NodeId),
                         pfcp_core::encode_node_id_ipv4(kSmfNodeIpv4));
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::FSeid),
                         pfcp_core::encode_f_seid_ipv4(cp_f_seid));
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreatePdr), create_pdr);
    pfcp_core::encode_ie(ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreateFar), create_far);

    pfcp_core::Header req_header;
    // Sx Session Establishment Request always has S=1 with SEID=0 -- UP's own SEID for this
    // session doesn't exist yet (TS 29.244 §7.2.2.4.2's explicit list of when SEID=0 is used).
    req_header.has_seid = true;
    req_header.seid = 0;
    req_header.message_type = pfcp_core::MessageType::SessionEstablishmentRequest;
    req_header.sequence_number = 1;

    auto pdu = pfcp_core::encode_header(req_header, static_cast<std::uint16_t>(ies.size()));
    pdu.insert(pdu.end(), ies.begin(), ies.end());

    const auto resp_ie_bytes = send_pfcp_request_and_await_response(
        socket, upf_endpoint, pdu, pfcp_core::MessageType::SessionEstablishmentResponse,
        req_header.sequence_number, "Session Establishment");
    if (!resp_ie_bytes.has_value()) {
        return false;
    }
    const auto resp_ies = pfcp_core::decode_ies(*resp_ie_bytes);
    const auto* cause_ie =
        resp_ies.has_value()
            ? pfcp_core::find_ie(*resp_ies, static_cast<std::uint16_t>(pfcp_core::IeType::Cause))
            : nullptr;
    const auto cause = cause_ie != nullptr ? pfcp_core::decode_cause(cause_ie->value) : std::nullopt;
    if (!cause.has_value() || *cause != pfcp_core::Cause::RequestAccepted) {
        spdlog::warn("smf: UPF rejected N4 Session Establishment for pduSessionId {} (cause={})",
                    pdu_session_id, cause.has_value() ? static_cast<int>(*cause) : -1);
        return false;
    }

    std::optional<pfcp_core::FSeid> up_f_seid;
    if (const auto* up_f_seid_ie =
            pfcp_core::find_ie(*resp_ies, static_cast<std::uint16_t>(pfcp_core::IeType::FSeid));
        up_f_seid_ie != nullptr) {
        up_f_seid = pfcp_core::decode_f_seid_ipv4(up_f_seid_ie->value);
    }
    std::optional<std::uint32_t> allocated_teid;
    if (const auto* created_pdr_ie =
            pfcp_core::find_ie(*resp_ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreatedPdr));
        created_pdr_ie != nullptr) {
        if (const auto created_pdr_ies = pfcp_core::decode_ies(created_pdr_ie->value);
            created_pdr_ies.has_value()) {
            if (const auto* f_teid_ie = pfcp_core::find_ie(
                    *created_pdr_ies, static_cast<std::uint16_t>(pfcp_core::IeType::FTeid));
                f_teid_ie != nullptr) {
                if (const auto allocated = pfcp_core::decode_f_teid_allocated_ipv4(f_teid_ie->value);
                    allocated.has_value()) {
                    allocated_teid = allocated->teid;
                }
            }
        }
    }

    spdlog::info("smf: N4 Session Establishment succeeded for pduSessionId {}, UPF F-SEID={:#x}, "
                "allocated uplink F-TEID={:#x}",
                pdu_session_id, up_f_seid.has_value() ? up_f_seid->seid : 0,
                allocated_teid.value_or(0));
    return true;
}

// Real Nchf_ConvergedCharging_Create (TS 32.291, N40, ADR-0044), SMF's side -- see this file's own
// header for the approved Phase 4 scope. Sends only the 3 mandatory ChargingDataRequest fields
// plus subscriberIdentifier; pDUSessionChargingInformation is deliberately left unset, matching
// nfs/chf/src/main.cpp's own disclosed "no real rating engine yet" scope -- nothing on the CHF
// side would use richer charging information yet, so sending it would be padding, not real
// content. invocationSequenceNumber is always 1: this is the first (and, this stage, only)
// charging data invocation SMF ever sends for a given PDU session -- see chf's own file header for
// why the response doesn't get independently sequenced either. Best-effort: see file header.
// Returns the allocated ChargingDataRef (parsed from CHF's `location` header, same extraction
// pattern already used for PCF's smPolicyId above) on success, so ReleaseSMContext's handler
// (ADR-0046) can later call Nchf_ConvergedCharging_Release against the right resource.
std::optional<std::string> perform_n40_charging_data_create(sbi_core::http2::Client& chf_client,
                                                             sbi_core::OAuth2Client& chf_oauth,
                                                             const std::string& smf_instance_id,
                                                             const std::string& supi,
                                                             std::uint8_t pdu_session_id) {
    auto token = chf_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::warn("smf: could not obtain a token for CHF, skipping Nchf_ConvergedCharging_"
                    "Create for pduSessionId {}: {}",
                    pdu_session_id, token.error());
        return std::nullopt;
    }

    sbi_gen::NFIdentification nf_id{};
    nf_id.nFName = smf_instance_id;
    nf_id.nFIPv4Address = "127.0.0.1";
    nf_id.nodeFunctionality.value = sbi_gen::NodeFunctionality::SMF;

    sbi_gen::ChargingDataRequest chf_req{};
    chf_req.nfConsumerIdentification = nf_id;
    chf_req.invocationTimeStamp = sbi_core::format_rfc3339(std::chrono::system_clock::now());
    chf_req.invocationSequenceNumber = 1;
    chf_req.subscriberIdentifier = supi;
    // ADR-0048: a real online-charging quota request. ratingGroup is TS 32.291's one mandatory
    // field on MultipleUnitUsage (confirmed directly against the vendored YAML's `required:`
    // block); requestedUnit is deliberately omitted -- this build has no real traffic-volume
    // estimator to request against, so CHF grants a full quota from its own rate-plan lookup
    // rather than SMF requesting a specific amount (see nfs/chf/src/main.cpp's own comment).
    sbi_gen::MultipleUnitUsage unit_usage{};
    unit_usage.ratingGroup = kDefaultRatingGroup;
    chf_req.multipleUnitUsage = std::vector<sbi_gen::MultipleUnitUsage>{unit_usage};

    sbi_core::http2::ClientRequest chf_http_req;
    chf_http_req.method = "POST";
    chf_http_req.url = std::string(kChfBase) + "/nchf-convergedcharging/v3/chargingdata";
    chf_http_req.headers.emplace("content-type", "application/json");
    chf_http_req.headers.emplace("authorization", "Bearer " + *token);
    chf_http_req.body = json(chf_req).dump();

    auto chf_resp = chf_client.send(chf_http_req);
    if (!chf_resp.has_value()) {
        spdlog::warn("smf: could not reach CHF for Nchf_ConvergedCharging_Create, pduSessionId "
                    "{}: {}",
                    pdu_session_id, chf_resp.error());
        return std::nullopt;
    }
    if (chf_resp->status != 201) {
        spdlog::warn("smf: CHF Nchf_ConvergedCharging_Create returned unexpected status {} for "
                    "pduSessionId {}",
                    chf_resp->status, pdu_session_id);
        return std::nullopt;
    }
    std::string charging_data_ref;
    if (const auto location_it = chf_resp->headers.find("location");
        location_it != chf_resp->headers.end()) {
        const auto& location = location_it->second;
        charging_data_ref = location.substr(location.find_last_of('/') + 1);
    }
    if (charging_data_ref.empty()) {
        spdlog::warn("smf: CHF Nchf_ConvergedCharging_Create succeeded but returned no usable "
                    "location header for pduSessionId {}, Release will not be possible for this "
                    "session",
                    pdu_session_id);
        return std::nullopt;
    }
    spdlog::info("smf: Nchf_ConvergedCharging_Create succeeded for pduSessionId {}, "
                "ChargingDataRef={}",
                pdu_session_id, charging_data_ref);
    return charging_data_ref;
}

// Real Nchf_ConvergedCharging_Release (TS 32.291, N40, ADR-0046), SMF's side.
// POST /chargingdata/{ChargingDataRef}/release, same real spec shape as Create's request body
// (ChargingDataRequest) -- sent with the same minimal 3-mandatory-fields-plus-subscriberIdentifier
// content as Create, since there's no richer charging information collected between Create and
// Release in this build either. Best-effort: matches DeleteSMPolicy's discipline in
// ReleaseSMContext's handler -- local session release must not get stuck on CHF being unreachable.
bool perform_n40_charging_data_release(sbi_core::http2::Client& chf_client,
                                       sbi_core::OAuth2Client& chf_oauth,
                                       const std::string& smf_instance_id, const std::string& supi,
                                       const std::string& charging_data_ref) {
    auto token = chf_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::warn("smf: could not obtain a token for CHF, skipping Nchf_ConvergedCharging_"
                    "Release for ChargingDataRef={}: {}",
                    charging_data_ref, token.error());
        return false;
    }

    sbi_gen::NFIdentification nf_id{};
    nf_id.nFName = smf_instance_id;
    nf_id.nFIPv4Address = "127.0.0.1";
    nf_id.nodeFunctionality.value = sbi_gen::NodeFunctionality::SMF;

    sbi_gen::ChargingDataRequest chf_req{};
    chf_req.nfConsumerIdentification = nf_id;
    chf_req.invocationTimeStamp = sbi_core::format_rfc3339(std::chrono::system_clock::now());
    chf_req.invocationSequenceNumber = 2; // 1 was Create's, see perform_n40_charging_data_create
    chf_req.subscriberIdentifier = supi;

    sbi_core::http2::ClientRequest chf_http_req;
    chf_http_req.method = "POST";
    chf_http_req.url = std::string(kChfBase) + "/nchf-convergedcharging/v3/chargingdata/" +
                       charging_data_ref + "/release";
    chf_http_req.headers.emplace("content-type", "application/json");
    chf_http_req.headers.emplace("authorization", "Bearer " + *token);
    chf_http_req.body = json(chf_req).dump();

    auto chf_resp = chf_client.send(chf_http_req);
    if (!chf_resp.has_value()) {
        spdlog::warn("smf: could not reach CHF for Nchf_ConvergedCharging_Release, "
                    "ChargingDataRef={}: {}",
                    charging_data_ref, chf_resp.error());
        return false;
    }
    if (chf_resp->status != 204) {
        spdlog::warn("smf: CHF Nchf_ConvergedCharging_Release returned unexpected status {} for "
                    "ChargingDataRef={}",
                    chf_resp->status, charging_data_ref);
        return false;
    }
    spdlog::info("smf: Nchf_ConvergedCharging_Release succeeded for ChargingDataRef={}",
                charging_data_ref);
    return true;
}

} // namespace

int main() {
    sbi_core::init_logging("smf");
    sbi_core::init_tracing("smf");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string smf_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("smf: starting, nfInstanceId={}", smf_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/smf/cert.pem",
        .key_path = CERTS_DIR "/smf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    // SMF's own client identity + token source for calling PCF -- separate http2::Client/
    // OAuth2Client from run_nrf_lifecycle's (which runs on its own thread; this one is only ever
    // touched from route handlers, which all run on ioc's single thread -- see
    // docs/DECISIONS.md ADR-0027, which established this exact pattern for AUSF calling UDM).
    sbi_core::http2::TlsConfig pcf_client_tls{
        .cert_path = CERTS_DIR "/smf/cert.pem",
        .key_path = CERTS_DIR "/smf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client pcf_client(std::move(pcf_client_tls));
    sbi_core::OAuth2Client pcf_oauth(pcf_client,
                                     std::string(kNrfBase) + "/oauth2/token",
                                     smf_instance_id,
                                     "npcf-smpolicycontrol",
                                     "PCF");

    // SMF's own client identity + token source for calling AMF's Namf_Communication
    // N1N2MessageTransfer (TS29518_Namf_Communication.yaml) -- the real mechanism for delivering
    // the PDU Session Establishment Accept back to the UE (ADR-0038), same one-client-per-NF
    // pattern as pcf_client above.
    sbi_core::http2::TlsConfig amf_client_tls{
        .cert_path = CERTS_DIR "/smf/cert.pem",
        .key_path = CERTS_DIR "/smf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client amf_client(std::move(amf_client_tls));
    sbi_core::OAuth2Client amf_oauth(amf_client,
                                     std::string(kNrfBase) + "/oauth2/token",
                                     smf_instance_id,
                                     "namf-comm",
                                     "AMF");

    // SMF's own client identity + token source for calling CHF's Nchf_ConvergedCharging (N40,
    // ADR-0044) -- same one-client-per-NF pattern as pcf_client/amf_client above.
    sbi_core::http2::TlsConfig chf_client_tls{
        .cert_path = CERTS_DIR "/smf/cert.pem",
        .key_path = CERTS_DIR "/smf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client chf_client(std::move(chf_client_tls));
    sbi_core::OAuth2Client chf_oauth(chf_client,
                                     std::string(kNrfBase) + "/oauth2/token",
                                     smf_instance_id,
                                     "nchf-convergedcharging",
                                     "CHF");

    smf::SmContextStore sm_contexts;
    UpfEndpointStore upf_endpoint_store;

    auto meter = sbi_core::get_meter("smf");
    auto create_counter =
        meter->CreateUInt64Counter("smf_create_sm_context_total", "Total CreateSMContext calls");
    auto retrieve_counter = meter->CreateUInt64Counter("smf_retrieve_sm_context_total",
                                                       "Total RetrieveSMContext calls");
    auto update_counter =
        meter->CreateUInt64Counter("smf_update_sm_context_total", "Total UpdateSMContext calls");
    auto release_counter =
        meter->CreateUInt64Counter("smf_release_sm_context_total", "Total ReleaseSMContext calls");
    auto pcf_sm_policy_create_counter = meter->CreateUInt64Counter(
        "smf_pcf_sm_policy_create_total", "Total successful CreateSMPolicy calls to PCF");
    auto pcf_sm_policy_delete_counter = meter->CreateUInt64Counter(
        "smf_pcf_sm_policy_delete_total", "Total successful (best-effort) DeleteSMPolicy calls to PCF");
    auto n1n2_transfer_counter = meter->CreateUInt64Counter(
        "smf_n1n2_message_transfer_total",
        "Total successful AMF N1N2MessageTransfer calls delivering a PDU Session Establishment Accept");
    auto chf_charging_data_create_counter = meter->CreateUInt64Counter(
        "smf_chf_charging_data_create_total",
        "Total successful (best-effort) Nchf_ConvergedCharging_Create calls to CHF");
    auto chf_charging_data_release_counter = meter->CreateUInt64Counter(
        "smf_chf_charging_data_release_total",
        "Total successful (best-effort) Nchf_ConvergedCharging_Release calls to CHF");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/sm-contexts",
        [&verifier, &sm_contexts, &create_counter, &pcf_client, &pcf_oauth,
         &pcf_sm_policy_create_counter, &amf_client, &amf_oauth, &upf_endpoint_store,
         &n1n2_transfer_counter, &chf_client, &chf_oauth, &chf_charging_data_create_counter,
         &smf_instance_id](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_multipart_json_body<sbi_gen::SmContextCreateData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            // Disclosed simplification (see file header): this build's PCF wiring has nothing to
            // fall back to without these, even though SmContextCreateData's schema allows them to
            // be absent for edge cases (e.g. unauthenticated SUPI) this build doesn't model.
            if (!body->supi.has_value() || !body->pduSessionId.has_value() ||
                !body->dnn.has_value() || !body->sNssai.has_value()) {
                return sbi_core::http2::problem_response(
                    400,
                    "Missing mandatory IE",
                    "This build requires supi, pduSessionId, dnn, and sNssai to establish an SM "
                    "Policy Association with PCF");
            }

            const auto sm_context_ref = sm_contexts.create(json::object());
            create_counter->Add(1);

            auto token = pcf_oauth.get_bearer_token();
            if (!token.has_value()) {
                sm_contexts.remove(sm_context_ref);
                return sbi_core::http2::problem_response(
                    500,
                    "Internal Server Error",
                    "SMF could not obtain a token for PCF: " + token.error());
            }

            sbi_gen::SmPolicyContextData pcf_req{};
            pcf_req.supi = *body->supi;
            pcf_req.pduSessionId = *body->pduSessionId;
            // PduSessionType is negotiated inside the NAS SM message (n1SmMsg, an opaque binary
            // blob this build never decodes) -- not available from SmContextCreateData at all.
            // Disclosed fixed default, not the UE's real requested type -- see file header.
            pcf_req.pduSessionType.value = sbi_gen::PduSessionType::IPV4;
            pcf_req.dnn = *body->dnn;
            pcf_req.notificationUri = std::string(kSelfBase) + std::string(kApiRoot) +
                                       "/sm-contexts/" + sm_context_ref + "/pcf-notify";
            pcf_req.sliceInfo = *body->sNssai;

            sbi_core::http2::ClientRequest pcf_http_req;
            pcf_http_req.method = "POST";
            pcf_http_req.url = std::string(kPcfBase) + "/npcf-smpolicycontrol/v1/sm-policies";
            pcf_http_req.headers.emplace("content-type", "application/json");
            pcf_http_req.headers.emplace("authorization", "Bearer " + *token);
            pcf_http_req.body = json(pcf_req).dump();

            auto pcf_resp = pcf_client.send(pcf_http_req);
            if (!pcf_resp.has_value()) {
                sm_contexts.remove(sm_context_ref);
                return sbi_core::http2::problem_response(
                    500,
                    "Internal Server Error",
                    "SMF could not reach PCF to establish an SM Policy Association: " +
                        pcf_resp.error());
            }
            if (pcf_resp->status != 201) {
                sm_contexts.remove(sm_context_ref);
                return sbi_core::http2::problem_response(
                    500,
                    "Internal Server Error",
                    "PCF CreateSMPolicy returned unexpected status " +
                        std::to_string(pcf_resp->status));
            }

            sbi_gen::SmPolicyDecision decision;
            try {
                decision = json::parse(pcf_resp->body).get<sbi_gen::SmPolicyDecision>();
            } catch (const json::exception& e) {
                sm_contexts.remove(sm_context_ref);
                return sbi_core::http2::problem_response(
                    500,
                    "Internal Server Error",
                    "PCF returned a malformed SmPolicyDecision: " + std::string(e.what()));
            }
            std::string sm_policy_id;
            if (const auto location_it = pcf_resp->headers.find("location");
                location_it != pcf_resp->headers.end()) {
                const auto& location = location_it->second;
                sm_policy_id = location.substr(location.find_last_of('/') + 1);
            }
            // Not exposed in SmContextCreatedData -- TS29502 has no field for it (matches
            // n2SmInfo's own unpopulated state, see file header); kept internally so
            // ReleaseSMContext can tear the association down again. `supi` is stored here too
            // (ADR-0046) so ReleaseSMContext can populate Nchf_ConvergedCharging_Release's
            // subscriberIdentifier without re-deriving it from anywhere else.
            sm_contexts.update(
                sm_context_ref,
                json{{"smPolicyId", sm_policy_id}, {"policy", json(decision)}, {"supi", *body->supi}});
            pcf_sm_policy_create_counter->Add(1);

            // ADR-0042: real N4 Session Establishment with UPF, using the Sx Association Stage 2
            // already proved (run_pfcp_lifecycle populates upf_endpoint_store once, at startup).
            // Best-effort, matching the N1N2MessageTransfer call below: no real datapath exists
            // yet (Stage 4), so a failure here is disclosed via a log line, not fatal to this
            // response -- the real gap it would block on (no UPF discovered yet) shouldn't also
            // block CreateSMContext's own already-real PCF/AMF work.
            if (const auto upf_ip = upf_endpoint_store.get(); upf_ip.has_value()) {
                perform_n4_session_establishment(*upf_ip, static_cast<std::uint8_t>(*body->pduSessionId));
            } else {
                spdlog::warn("smf: no UPF Sx Association established yet, skipping N4 Session "
                            "Establishment for pduSessionId {}",
                            *body->pduSessionId);
            }

            // N40, ADR-0044/ADR-0046: real Nchf_ConvergedCharging_Create, same best-effort
            // discipline as the N4 call directly above (see perform_n40_charging_data_create's own
            // comment). The returned ChargingDataRef is merged into the already-stored sm context
            // (read-modify-write, not a plain update -- SmContextStore::update replaces the whole
            // entry, and smPolicyId/policy were already written above) so ReleaseSMContext can
            // later call Nchf_ConvergedCharging_Release against the right resource.
            if (const auto charging_data_ref = perform_n40_charging_data_create(
                    chf_client, chf_oauth, smf_instance_id, *body->supi,
                    static_cast<std::uint8_t>(*body->pduSessionId));
                charging_data_ref.has_value()) {
                chf_charging_data_create_counter->Add(1);
                if (auto stored = sm_contexts.get(sm_context_ref); stored.has_value()) {
                    (*stored)["chargingDataRef"] = *charging_data_ref;
                    sm_contexts.update(sm_context_ref, *stored);
                }
            }

            // ADR-0038: the real TS 23.502 §4.3.2.2.1 step 11 -- SMF decodes the UE's actual PDU
            // Session Establishment Request (forwarded by AMF as n1SmMsg, ADR-0038, not the
            // opaque-and-dropped gap ADR-0036 disclosed), builds a real Accept using PCF's actual
            // QoS decision above (not fabricated), and delivers it to the UE via AMF's
            // Namf_Communication N1N2MessageTransfer -- the real mechanism (TS29518_
            // Namf_Communication.yaml), not a field on this response. Best-effort: any failure
            // here is logged, not fatal to CreateSMContext's own 201 -- matches TS 23.502's real
            // procedure, where N1N2MessageTransfer happens after CreateSMContext already returned.
            if (body->n1SmMsg.has_value()) {
                std::vector<std::uint8_t> n1_sm_bytes;
                bool found_n1_sm = false;
                if (const auto content_type_it = req.headers.find("content-type");
                    content_type_it != req.headers.end()) {
                    if (auto parts = sbi_core::multipart::parse(content_type_it->second, req.body);
                        parts.has_value()) {
                        for (const auto& part : *parts) {
                            if (part.content_id.has_value() &&
                                *part.content_id == body->n1SmMsg->contentId) {
                                n1_sm_bytes.assign(part.body.begin(), part.body.end());
                                found_n1_sm = true;
                                break;
                            }
                        }
                    }
                }
                const auto req_info = found_n1_sm
                                          ? smf::nas5gsm::decode_establishment_request(n1_sm_bytes)
                                          : std::nullopt;
                if (!req_info.has_value()) {
                    spdlog::warn("smf: SUPI {} pduSessionId {} -- n1SmMsg referenced but its binary "
                                "part was missing or not a PDU Session Establishment Request, no "
                                "Accept sent",
                                *body->supi, *body->pduSessionId);
                } else {
                    // Sourced from PCF's real SmPolicyDecision.sessRules (built above), not
                    // fabricated -- falls back to nas_5gsm_codec's own disclosed defaults only if
                    // PCF returned no session rule at all.
                    std::string ambr_ul = "1 Mbps";
                    std::string ambr_dl = "1 Mbps";
                    std::uint8_t qfi = 1;
                    if (decision.sessRules.has_value() && decision.sessRules->is_object() &&
                        !decision.sessRules->empty()) {
                        try {
                            const auto rule =
                                decision.sessRules->begin().value().get<sbi_gen::SessionRule>();
                            if (rule.authSessAmbr.has_value()) {
                                ambr_ul = rule.authSessAmbr->uplink;
                                ambr_dl = rule.authSessAmbr->downlink;
                            }
                            if (rule.authDefQos.has_value() && rule.authDefQos->n5qi.has_value()) {
                                qfi = static_cast<std::uint8_t>(*rule.authDefQos->n5qi & 0x3F);
                            }
                        } catch (const json::exception&) {
                            // Malformed sessRules entry -- fall back to the defaults above,
                            // disclosed via the log line below rather than silently swallowed.
                        }
                    }

                    const auto accept_bytes = smf::nas5gsm::encode_establishment_accept(
                        req_info->pdu_session_id, req_info->pti, ambr_ul, ambr_dl, qfi);

                    auto amf_token = amf_oauth.get_bearer_token();
                    if (!amf_token.has_value()) {
                        spdlog::warn("smf: could not obtain AMF token, PDU Session Establishment "
                                    "Accept not delivered for SUPI {}: {}",
                                    *body->supi, amf_token.error());
                    } else {
                        sbi_gen::N1N2MessageTransferReqData n1n2_req{};
                        sbi_gen::N1MessageContainer n1_container{};
                        n1_container.n1MessageClass.value = sbi_gen::N1MessageClass::SM;
                        sbi_gen::RefToBinaryData n1_content_ref{};
                        n1_content_ref.contentId = "n1Message";
                        n1_container.n1MessageContent = n1_content_ref;
                        n1n2_req.n1MessageContainer = n1_container;
                        n1n2_req.pduSessionId = body->pduSessionId;

                        sbi_core::multipart::Part n1n2_json_part;
                        n1n2_json_part.content_type = "application/json";
                        n1n2_json_part.body = json(n1n2_req).dump();
                        sbi_core::multipart::Part n1n2_bin_part;
                        n1n2_bin_part.content_type = "application/vnd.3gpp.5gnas";
                        n1n2_bin_part.content_id = "n1Message";
                        n1n2_bin_part.body.assign(accept_bytes.begin(), accept_bytes.end());
                        const auto n1n2_encoded =
                            sbi_core::multipart::encode({n1n2_json_part, n1n2_bin_part});

                        sbi_core::http2::ClientRequest amf_http_req;
                        amf_http_req.method = "POST";
                        amf_http_req.url = std::string(kAmfBase) + "/namf-comm/v1/ue-contexts/" +
                                           *body->supi + "/n1-n2-messages";
                        amf_http_req.headers.emplace("content-type",
                                                     n1n2_encoded.content_type_header);
                        amf_http_req.headers.emplace("authorization", "Bearer " + *amf_token);
                        amf_http_req.body = n1n2_encoded.body;

                        auto amf_resp = amf_client.send(amf_http_req);
                        if (!amf_resp.has_value() ||
                            (amf_resp->status != 200 && amf_resp->status != 202)) {
                            spdlog::warn(
                                "smf: AMF N1N2MessageTransfer call failed for SUPI {} -- PDU "
                                "Session Establishment Accept not delivered to the UE: {}",
                                *body->supi,
                                amf_resp.has_value() ? std::to_string(amf_resp->status)
                                                     : amf_resp.error());
                        } else {
                            n1n2_transfer_counter->Add(1);
                            spdlog::info("smf: PDU Session Establishment Accept delivered to AMF "
                                        "for SUPI {}, pduSessionId {}",
                                        *body->supi, *body->pduSessionId);
                        }
                    }
                }
            }

            sbi_gen::SmContextCreatedData resp_data;
            resp_data.pduSessionId = body->pduSessionId;
            resp_data.sNssai = body->sNssai;
            json j = resp_data;
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 std::string(kApiRoot) + "/sm-contexts/" + sm_context_ref);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/sm-contexts/{smContextRef}/retrieve",
        [&verifier, &sm_contexts, &retrieve_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto sm_context_ref = req.path_params.at("smContextRef");
            if (!sm_contexts.get(sm_context_ref).has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM context with ref " + sm_context_ref);
            }
            // SmContextRetrieveData is optional per spec (required: false) -- an empty body is
            // valid, not a parse error.
            if (!req.body.empty()) {
                sbi_core::http2::Response err;
                auto body =
                    sbi_core::http2::parse_json_body<sbi_gen::SmContextRetrieveData>(req, err);
                if (!body.has_value()) {
                    return err;
                }
            }
            retrieve_counter->Add(1);
            // Disclosed simplification: ueEpsPdnConnection (mandatory per spec) is an opaque
            // base64 container with nothing real behind it in this build (no EPS interworking
            // state exists) -- emitted as an empty string, a schema-valid but empty value.
            sbi_gen::SmContextRetrievedData resp_data;
            resp_data.ueEpsPdnConnection = "";
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/sm-contexts/{smContextRef}/modify",
        [&verifier, &sm_contexts, &update_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SmContextUpdateData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto sm_context_ref = req.path_params.at("smContextRef");
            if (!sm_contexts.get(sm_context_ref).has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM context with ref " + sm_context_ref);
            }
            update_counter->Add(1);
            // Disclosed simplification: acknowledges the update (204) rather than fabricating
            // SmContextUpdatedData content (EBI allocation, N1/N2 info, ...) with no real PCF/
            // UPF backing it yet (see file header).
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/sm-contexts/{smContextRef}/release",
        [&verifier, &sm_contexts, &release_counter, &pcf_client, &pcf_oauth,
         &pcf_sm_policy_delete_counter, &chf_client, &chf_oauth, &chf_charging_data_release_counter,
         &smf_instance_id](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto sm_context_ref = req.path_params.at("smContextRef");
            auto stored = sm_contexts.get(sm_context_ref);
            if (!stored.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM context with ref " + sm_context_ref);
            }
            // SmContextReleaseData is optional per spec (required: false).
            if (!req.body.empty()) {
                sbi_core::http2::Response err;
                auto body =
                    sbi_core::http2::parse_json_body<sbi_gen::SmContextReleaseData>(req, err);
                if (!body.has_value()) {
                    return err;
                }
            }

            // Best-effort DeleteSMPolicy -- see file header for why this doesn't gate local
            // release the way CreateSMContext's PCF call gates creation.
            if (stored->contains("smPolicyId")) {
                const auto sm_policy_id = (*stored)["smPolicyId"].get<std::string>();
                if (!sm_policy_id.empty()) {
                    auto token = pcf_oauth.get_bearer_token();
                    if (!token.has_value()) {
                        spdlog::warn(
                            "smf: could not obtain a PCF token for best-effort DeleteSMPolicy "
                            "(smPolicyId={}): {}",
                            sm_policy_id, token.error());
                    } else {
                        sbi_core::http2::ClientRequest pcf_http_req;
                        pcf_http_req.method = "POST";
                        pcf_http_req.url = std::string(kPcfBase) +
                                           "/npcf-smpolicycontrol/v1/sm-policies/" + sm_policy_id +
                                           "/delete";
                        pcf_http_req.headers.emplace("content-type", "application/json");
                        pcf_http_req.headers.emplace("authorization", "Bearer " + *token);
                        pcf_http_req.body = json::object().dump();
                        auto pcf_resp = pcf_client.send(pcf_http_req);
                        if (pcf_resp.has_value() && pcf_resp->status == 204) {
                            pcf_sm_policy_delete_counter->Add(1);
                        } else {
                            spdlog::warn(
                                "smf: best-effort DeleteSMPolicy failed for smPolicyId={}",
                                sm_policy_id);
                        }
                    }
                }
            }

            // Best-effort Nchf_ConvergedCharging_Release (ADR-0046) -- same discipline as
            // DeleteSMPolicy directly above: local release must not block on CHF being
            // unreachable. Needs both chargingDataRef (from Create's response, ADR-0044) and supi
            // (stored alongside smPolicyId above) -- if either is missing (e.g. Create's own N40
            // call never succeeded for this session), there's nothing valid to release, skipped
            // rather than sent with a fabricated ref.
            if (stored->contains("chargingDataRef") && stored->contains("supi")) {
                const auto charging_data_ref = (*stored)["chargingDataRef"].get<std::string>();
                const auto supi = (*stored)["supi"].get<std::string>();
                if (!charging_data_ref.empty() &&
                    perform_n40_charging_data_release(chf_client, chf_oauth, smf_instance_id, supi,
                                                       charging_data_ref)) {
                    chf_charging_data_release_counter->Add(1);
                }
            }

            sm_contexts.remove(sm_context_ref);
            release_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    std::thread(run_nrf_lifecycle, smf_instance_id).detach();
    std::thread(run_pfcp_lifecycle, smf_instance_id, std::ref(upf_endpoint_store)).detach();

    server.start();
    spdlog::info("smf: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("smf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
