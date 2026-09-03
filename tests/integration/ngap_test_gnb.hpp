#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ADR-0264: a real gNB, for tests.
//
// Why this exists. Every NGAP procedure this project has built -- the whole N2 handover chain
// across ADR-0090/0095/0096/0248/0249/0258/0261 -- has been verified by construction and review,
// never end to end, because UERANSIM (ADR-0016, the vendored RAN simulator) implements NO handover
// procedure in its gNB at all: `HandoverRequired` appears in its generated ASN.1 and never once in
// `src/gnb/`. That is a gap in verification, not in the simulator's honesty, and it is why
// `SctpSocket::connect()` was added back at ADR-0090 for "a real hand-crafted NGAP test client"
// that was never committed. This is that client, committed.
//
// What it is: a real SCTP association to AMF's real N2 listener, carrying real Aligned-PER NGAP
// PDUs built with this project's own `ngap_core` codec and `ngap_generated` ASN.1 -- the same
// codec AMF itself uses. It is NOT a simulated or stubbed transport.
//
// Deliberate design choice: this header exposes no ASN.1 type. Tests deal in byte vectors and the
// small set of questions below, so a test including this does not pull the generated NGAP headers
// into its own translation unit, and the ASN.1 lifetime rules stay inside the .cpp where they can
// be got right once.

namespace nf_test {

class NgapTestGnb {
public:
    NgapTestGnb();
    ~NgapTestGnb();

    NgapTestGnb(const NgapTestGnb&) = delete;
    NgapTestGnb& operator=(const NgapTestGnb&) = delete;

    // Establishes the SCTP association. Retries because AMF's NGAP listener comes up on its own
    // thread after the process starts. Returns false if it never became connectable.
    bool connect(const std::string& address, std::uint16_t port, int max_attempts = 80);

    // Real NGSetupRequest carrying a real GlobalRANNodeID/globalGNB-ID (PLMNIdentity + a 32-bit
    // GNB-ID bit string), then awaits the response. Returns true only on a real NGSetupResponse.
    // `gnb_id` distinguishes source from target when a test runs two of these.
    bool ng_setup(std::uint32_t gnb_id);

    void send_raw(const std::vector<std::uint8_t>& pdu_bytes);

    // Blocking receive of one PDU. Empty vector means the peer closed the association.
    std::vector<std::uint8_t> receive_raw();

    // --- PDU builders. Each returns real PER bytes, or empty on an encode failure. ---

    // Mandatory IE set per HandoverRequiredIEs (specs/NGAP/ngap-17.9.asn): AMF-UE-NGAP-ID(10),
    // RAN-UE-NGAP-ID(85), HandoverType(29), Cause(15), TargetID(105),
    // PDUSessionResourceListHORqd(61), SourceToTarget-TransparentContainer(101). `target_gnb_id`
    // names the gNB this handover is towards, in the same GlobalGNB-ID form ng_setup registers.
    std::vector<std::uint8_t> build_handover_required(std::uint64_t amf_ue_id,
                                                      std::uint32_t ran_ue_id,
                                                      std::uint32_t target_gnb_id,
                                                      std::uint8_t pdu_session_id);

    // Mandatory IE set per HandoverCancelIEs: AMF-UE-NGAP-ID(10), RAN-UE-NGAP-ID(85), Cause(15).
    std::vector<std::uint8_t> build_handover_cancel(std::uint64_t amf_ue_id,
                                                    std::uint32_t ran_ue_id);

    // --- Questions a test can ask about a received PDU, without touching ASN.1 itself. ---

    enum class Outcome { Initiating, Successful, Unsuccessful, Undecodable };

    struct PduSummary {
        Outcome outcome = Outcome::Undecodable;
        long procedure_code = -1;
    };

    static PduSummary summarize(const std::vector<std::uint8_t>& pdu_bytes);

    // NGAP procedure codes, read from specs/NGAP/ngap-17.9.asn rather than remembered.
    static constexpr long kProcHandoverCancel = 10;
    static constexpr long kProcHandoverPreparation = 12;
    static constexpr long kProcNgSetup = 21;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace nf_test
