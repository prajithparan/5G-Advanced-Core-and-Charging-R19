// Round-trip and hand-crafted-input tests for sbi_core::multipart (RFC 2046/2387 multipart/related
// codec -- see libs/sbi-core/include/sbi_core/multipart.hpp for why this exists and what it does
// and doesn't verify against a real peer).

#include "sbi_core/multipart.hpp"

#include <gtest/gtest.h>

namespace {

using sbi_core::multipart::encode;
using sbi_core::multipart::is_multipart_related;
using sbi_core::multipart::parse;
using sbi_core::multipart::Part;

} // namespace

TEST(Multipart, IsMultipartRelatedDetection) {
    EXPECT_TRUE(
        is_multipart_related("multipart/related; boundary=\"abc\"; type=\"application/json\""));
    EXPECT_TRUE(
        is_multipart_related("Multipart/Related;boundary=abc")); // case-insensitive media type
    EXPECT_TRUE(is_multipart_related("multipart/related"));      // no params at all
    EXPECT_FALSE(is_multipart_related("application/json"));
    EXPECT_FALSE(is_multipart_related("multipart/form-data; boundary=abc"));
}

TEST(Multipart, EncodeThenParseTwoPartsRoundTrips) {
    std::vector<Part> parts;
    Part json_part;
    json_part.content_type = "application/json";
    json_part.body = R"({"pduSessionId":5})";
    parts.push_back(json_part);

    Part binary_part;
    binary_part.content_type = "application/vnd.3gpp.ngap";
    binary_part.content_id = "n2SmInfo";
    binary_part.body = std::string("\x00\x01\x02\xff", 4); // arbitrary opaque bytes, not text
    parts.push_back(binary_part);

    const auto encoded = encode(parts);
    ASSERT_TRUE(is_multipart_related(encoded.content_type_header));

    auto parsed = parse(encoded.content_type_header, encoded.body);
    ASSERT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error());
    ASSERT_EQ(parsed->size(), 2U);

    EXPECT_EQ((*parsed)[0].content_type, "application/json");
    EXPECT_FALSE((*parsed)[0].content_id.has_value());
    EXPECT_EQ((*parsed)[0].body, json_part.body);

    EXPECT_EQ((*parsed)[1].content_type, "application/vnd.3gpp.ngap");
    ASSERT_TRUE((*parsed)[1].content_id.has_value());
    EXPECT_EQ(*(*parsed)[1].content_id, "n2SmInfo");
    EXPECT_EQ((*parsed)[1].body, binary_part.body);
}

TEST(Multipart, EncodeThenParseSinglePartRoundTrips) {
    std::vector<Part> parts;
    Part json_part;
    json_part.content_type = "application/json";
    json_part.body = R"({"onlyPart":true})";
    parts.push_back(json_part);

    const auto encoded = encode(parts);
    auto parsed = parse(encoded.content_type_header, encoded.body);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 1U);
    EXPECT_EQ((*parsed)[0].body, json_part.body);
}

// Hand-crafted body shaped like TS29518_Namf_Communication.yaml's CreateUEContext request
// (jsonData + one binaryDataN2Information part) -- proves the codec works against literal wire
// bytes, not just its own encode() output.
TEST(Multipart, ParsesHandCraftedRealShapedBody) {
    const std::string content_type =
        "multipart/related; boundary=\"boundary42\"; type=\"application/json\"";
    const std::string body = "--boundary42\r\n"
                             "Content-Type: application/json\r\n"
                             "\r\n"
                             "{\"supi\":\"imsi-999700000000001\"}\r\n"
                             "--boundary42\r\n"
                             "Content-Type: application/vnd.3gpp.ngap\r\n"
                             "Content-Id: <n2Info>\r\n" // angle-bracket form -- leniency check
                             "\r\n"
                             "NGAPBYTES\r\n"
                             "--boundary42--\r\n";

    auto parsed = parse(content_type, body);
    ASSERT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error());
    ASSERT_EQ(parsed->size(), 2U);
    EXPECT_EQ((*parsed)[0].content_type, "application/json");
    EXPECT_EQ((*parsed)[0].body, "{\"supi\":\"imsi-999700000000001\"}");
    EXPECT_EQ((*parsed)[1].content_type, "application/vnd.3gpp.ngap");
    ASSERT_TRUE((*parsed)[1].content_id.has_value());
    EXPECT_EQ(*(*parsed)[1].content_id, "n2Info"); // brackets stripped
    EXPECT_EQ((*parsed)[1].body, "NGAPBYTES");
}

TEST(Multipart, RejectsNonMultipartContentType) {
    auto parsed = parse("application/json", "{}");
    ASSERT_FALSE(parsed.has_value());
}

TEST(Multipart, RejectsMissingBoundaryParameter) {
    auto parsed =
        parse("multipart/related; type=\"application/json\"", "--x\r\n\r\nbody\r\n--x--\r\n");
    ASSERT_FALSE(parsed.has_value());
}

TEST(Multipart, RejectsBodyWithNoDelimiter) {
    auto parsed =
        parse("multipart/related; boundary=\"nomatch\"", "just some bytes, no boundary here");
    ASSERT_FALSE(parsed.has_value());
}

TEST(Multipart, RejectsUnterminatedBody) {
    const std::string content_type = "multipart/related; boundary=\"b\"";
    const std::string body = "--b\r\nContent-Type: application/json\r\n\r\n{}"; // no closing --b--
    auto parsed = parse(content_type, body);
    ASSERT_FALSE(parsed.has_value());
}
