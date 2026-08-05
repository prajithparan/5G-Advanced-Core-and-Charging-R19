#pragma once

#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <vector>

// multipart/related (RFC 2046 "multipart", RFC 2387 "related") codec. 3GPP SBI reuses this
// standard IETF wire format verbatim for request/response bodies that carry a JSON part alongside
// opaque binary parts (N1 NAS messages, N2 NGAP PDUs, GTP-C messages, ...) -- see e.g.
// TS29518_Namf_Communication.yaml's CreateUEContext, TS29502_Nsmf_PDUSession.yaml's
// PostSmContexts. This is NOT a 3GPP-specific format: the framing (boundary delimiters, per-part
// headers, CRLF conventions) below is transcribed from RFC 2046/2387, not from any 3GPP TS -- the
// only 3GPP-specific knowledge is which named parts a given operation expects, which lives in each
// NF's own handler code, not here.
//
// One genuinely unverified assumption, disclosed rather than asserted as fact: whether 3GPP peers
// send/expect Content-Id header values wrapped in RFC 2045 msg-id angle brackets (`<foo>`) or as a
// bare token matching the JSON body's `RefToBinaryData.contentId` string verbatim. The OpenAPI YAML
// only declares `Content-Id: {schema: {type: string}}`, which doesn't settle this, and there is no
// real 3GPP SBI peer in this lab to interop-test against yet (simulators/ransim speaks NGAP/NAS to
// a gNB, not Namf_Communication/Nsmf_PDUSession multipart bodies). This codec is lenient on
// parsing (accepts either form, stripping brackets if present) and encodes WITHOUT brackets (the
// bare-token convention observed in other open-source 5GC implementations' interop reports, not
// verified firsthand). Flagged for revisit the first time this actually needs to interop with a
// real external SBI peer. See docs/DECISIONS.md ADR-0020.

namespace sbi_core::multipart {

struct Part {
    std::string content_type;
    std::optional<std::string> content_id; // nullopt for the root part (never sent one, per the
                                           // 3GPP operations examined: the root/jsonData part is
                                           // always first with no explicit `start=` parameter).
    std::string body;
};

// content_type_header: the raw Content-Type header value, e.g.
//   `multipart/related; boundary="abc123"; type="application/json"`
// body: the raw request/response body bytes.
// Returns parts in wire order (first element is the root/jsonData part) or an error string.
tl::expected<std::vector<Part>, std::string> parse(const std::string& content_type_header,
                                                   const std::string& body);

struct Encoded {
    std::string content_type_header; // full header value, including boundary + type params
    std::string body;
};

// parts[0] must be the root (jsonData) part -- its content_type becomes the encoded header's
// `type=` parameter per RFC 2387 clause 3.1.
Encoded encode(const std::vector<Part>& parts);

// True if content_type_header's media type is multipart/related (case-insensitive, ignoring
// parameters) -- what NF handler code checks before choosing the multipart path over the plain
// application/json path for operations that support both (see e.g. AMF's UEContextTransfer).
bool is_multipart_related(const std::string& content_type_header);

} // namespace sbi_core::multipart
