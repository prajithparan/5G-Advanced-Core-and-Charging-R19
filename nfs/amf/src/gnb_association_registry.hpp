#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ngap_core/sctp_socket.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Private to nfs/amf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #100): the real, previously-missing
// architectural piece a genuine N2-based handover (HandoverRequired/.../HandoverNotify, TS 38.413
// §8.4.2-8.4.4) needs and PathSwitchRequest (ADR-0090) didn't. Every NGAP procedure this project
// handled before this one either stays on ONE already-open association (registration, NAS
// transport) or arrives on a brand-new association after the OLD one already closed
// (PathSwitchRequest, ngap_task.hpp's own header comment). Real N2 handover is different: AMF
// must relay between the SOURCE gNB's and TARGET gNB's own *simultaneously open* NG-C
// associations -- send HandoverRequest on the target's, receive HandoverRequestAcknowledge back
// on that same target association, then reply on the source's. `run_ngap_lifecycle`'s own
// pre-existing accept loop (ADR-0031: "Single-association handling, sequentially, on this same
// thread") cannot hold two associations open at once at all -- fixed alongside this registry (see
// ngap_task.cpp's own run_ngap_lifecycle: now one std::thread per accepted association).
//
// Keyed by a real, spec-derived gNB identity: the PER-encoded bytes of the real GlobalGNB-ID IE
// (PLMNIdentity + GNB-ID bit string, TS 38.413 §9.3.1.6) captured from that gNB's own real
// NGSetupRequest -- not an invented label. This lab's real, disclosed scope: only gNB
// (GlobalGNB-ID) is supported as a RAN node identity, matching every other NGAP handler in this
// file's own gNB-only scope; GlobalNgENB-ID/GlobalN3IWF-ID/TNGF/TWIF/W-AGF (the real ASN.1
// CHOICE's other arms) are not this project's access network and are rejected, not silently
// misparsed.
namespace amf::ngap {

// One real relay operation in flight per target gNB at a time -- a real, disclosed lab-scope
// simplification (this project's own established "one gNB/one UE at a time" precedent, ADR-0031,
// extended to "one cross-association relay per target gNB at a time" rather than a generic
// per-transaction-ID correlation scheme a production AMF handling concurrent handovers to the
// same gNB would need).
struct GnbAssociation {
    ngap_core::SctpSocket* socket = nullptr;
    std::mutex send_mutex;

    // Guards the fields below -- set by send_and_await_reply before sending, consumed by
    // deliver_reply_if_pending (called from the TARGET association's OWN receiving thread, see
    // ngap_task.cpp's dispatch loop) when a HandoverRequestAcknowledge/HandoverFailure arrives
    // while a relay is outstanding.
    std::mutex reply_mutex;
    std::condition_variable reply_cv;
    bool awaiting_reply = false;
    std::optional<std::vector<std::uint8_t>> reply_bytes;
};

class GnbAssociationRegistry {
public:
    // Real UPSERT -- a gNB reconnecting (fresh NGSetupRequest after an association drop) replaces
    // its prior, now-dead entry rather than being rejected as a duplicate.
    void register_gnb(const std::vector<std::uint8_t>& gnb_id, ngap_core::SctpSocket* socket);
    void unregister_gnb(const std::vector<std::uint8_t>& gnb_id);

    // Sends `pdu_bytes` on the target gNB's live association, then blocks (up to `timeout`) for
    // deliver_reply_if_pending to be called by that association's own receiving thread. Returns
    // nullopt if the gNB isn't registered, the send fails, or the wait times out.
    std::optional<std::vector<std::uint8_t>>
    send_and_await_reply(const std::vector<std::uint8_t>& gnb_id,
                         const std::vector<std::uint8_t>& pdu_bytes,
                         std::chrono::milliseconds timeout);

    // Called by a registered gNB's own receiving thread when it decodes a PDU that looks like a
    // reply to an outstanding relay (HandoverRequestAcknowledge/HandoverFailure). Returns true if
    // a waiter consumed it (caller should NOT also dispatch it through the normal handler path);
    // false if no relay was outstanding for this gNB (caller dispatches normally, or warns).
    bool deliver_reply_if_pending(const std::vector<std::uint8_t>& gnb_id,
                                  std::vector<std::uint8_t> reply_bytes);

    // Fire-and-forget send (no reply expected) on a registered gNB's live association -- used to
    // relay HandoverCommand/HandoverPreparationFailure back to the SOURCE gNB from whatever
    // thread is handling the TARGET's HandoverRequestAcknowledge. Returns false if the gNB isn't
    // registered.
    bool send(const std::vector<std::uint8_t>& gnb_id, const std::vector<std::uint8_t>& pdu_bytes);

private:
    std::mutex registry_mutex_;
    std::unordered_map<std::string, std::shared_ptr<GnbAssociation>> associations_;

    std::shared_ptr<GnbAssociation> find(const std::vector<std::uint8_t>& gnb_id);
};

} // namespace amf::ngap
