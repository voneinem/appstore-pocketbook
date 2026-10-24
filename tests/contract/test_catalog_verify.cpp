// T027: the consumer rules of docs/contracts/catalog-format.md (User Story 1, SC-001).
#include "pbstore/catalog_verify.h"
#include "keys.h"
#include "sample_catalog.h"
#include "test_support.h"

using namespace pbstore;

static bool has_rule(const VerifyResult& r, const char* id) {
    for (const Finding& f : r.findings)
        if (f.rule_id == id) return true;
    return false;
}

struct Signed {
    std::string bytes, sig;
};
static Signed make(const pbtest::KeyPair& k, int64_t serial, const std::string& schema = "1.0") {
    Catalog c = pbtest::sample_catalog(serial);
    c.schema_version = schema;
    Signed s;
    s.bytes = write_catalog(c);
    s.sig = pbtest::sign_text(k, s.bytes);
    return s;
}

PB_TEST(a_genuine_catalog_is_accepted_and_its_serial_returned) {
    auto k = pbtest::make_keypair();
    Signed s = make(k, 1234);
    VerifyResult r = verify_catalog(s.bytes, s.sig, {k.pub}, 0);
    PB_CHECK(r.ok());
    PB_CHECK(r.findings.empty());
    PB_CHECK(r.catalog.has_value());
    PB_CHECK_EQ(r.serial, int64_t(1234));
    PB_CHECK_EQ(r.catalog->apps.size(), size_t(2));
}

PB_TEST(content_changed_after_signing_is_rejected) {
    auto k = pbtest::make_keypair();
    Signed s = make(k, 5);
    s.bytes[s.bytes.find("alpha-app")] = 'b';
    VerifyResult r = verify_catalog(s.bytes, s.sig, {k.pub}, 0);
    PB_CHECK(!r.ok());
    PB_CHECK(has_rule(r, "E-CAT-01"));
    PB_CHECK(!r.catalog.has_value());   // nothing is offered from it
}

PB_TEST(a_key_that_is_not_trusted_is_rejected) {
    auto signer = pbtest::make_keypair(), other = pbtest::make_keypair();
    Signed s = make(signer, 5);
    VerifyResult r = verify_catalog(s.bytes, s.sig, {other.pub}, 0);
    PB_CHECK(has_rule(r, "E-CAT-01"));
    PB_CHECK(!r.catalog.has_value());
    PB_CHECK(has_rule(verify_catalog(s.bytes, s.sig, {}, 0), "E-CAT-01"));   // empty trust list trusts nothing
}

PB_TEST(a_lower_serial_than_the_last_accepted_is_a_rollback) {
    auto k = pbtest::make_keypair();
    Signed s = make(k, 100);
    VerifyResult r = verify_catalog(s.bytes, s.sig, {k.pub}, 101);
    PB_CHECK(!r.ok());
    PB_CHECK(has_rule(r, "E-CAT-04"));
    PB_CHECK(!r.catalog.has_value());
}

PB_TEST(an_equal_serial_is_accepted_as_unchanged) {
    auto k = pbtest::make_keypair();
    Signed s = make(k, 100);
    VerifyResult r = verify_catalog(s.bytes, s.sig, {k.pub}, 100);
    PB_CHECK(r.ok());
    PB_CHECK_EQ(r.serial, int64_t(100));
    PB_CHECK(verify_catalog(s.bytes, s.sig, {k.pub}, 99).ok());
}

PB_TEST(a_signed_newer_major_says_it_needs_updating_and_offers_nothing) {
    auto k = pbtest::make_keypair();
    Signed s = make(k, 100, "2.0");
    // the newer schema may look different; make the content unparsable for schema 1 on purpose
    std::string bytes = "{\n  \"schema_version\": \"2.0\",\n  \"serial\": 100,\n  \"apps\": {\"new\": \"shape\"}\n}\n";
    std::string sig = pbtest::sign_text(k, bytes);
    VerifyResult r = verify_catalog(bytes, sig, {k.pub}, 0);
    PB_CHECK(!r.ok());
    PB_CHECK(has_rule(r, "E-CAT-03"));
    bool says_update = false;
    for (const Finding& f : r.findings) if (f.message.find("needs updating") != std::string::npos) says_update = true;
    PB_CHECK(says_update);
    PB_CHECK(!r.catalog.has_value());
    // and a newer major is only reported after the signature was verified
    VerifyResult unsigned_r = verify_catalog(bytes, pbtest::sign_text(pbtest::make_keypair(), bytes), {k.pub}, 0);
    PB_CHECK(has_rule(unsigned_r, "E-CAT-01"));
    PB_CHECK(!has_rule(unsigned_r, "E-CAT-03"));
    (void)s;
}

PB_TEST(a_catalog_larger_than_one_mebibyte_is_rejected) {
    auto k = pbtest::make_keypair();
    std::string big(1024 * 1024 + 1, ' ');
    VerifyResult r = verify_catalog(big, pbtest::sign_text(k, big), {k.pub}, 0);
    PB_CHECK(has_rule(r, "E-CAT-05"));
    PB_CHECK(!r.catalog.has_value());
    // exactly 1 MiB of valid content is not too large (size check only)
    Signed s = make(k, 7);
    PB_CHECK(verify_catalog(s.bytes, s.sig, {k.pub}, 0).ok());
}

PB_TEST(content_is_not_interpreted_before_the_signature_is_checked) {
    auto k = pbtest::make_keypair(), other = pbtest::make_keypair();
    const std::string garbage = "{ this is not json";
    VerifyResult r = verify_catalog(garbage, pbtest::sign_text(other, garbage), {k.pub}, 0);
    PB_CHECK(has_rule(r, "E-CAT-01"));
    PB_CHECK(!has_rule(r, "E-CAT-02"));
    // signed garbage is then malformed
    VerifyResult m = verify_catalog(garbage, pbtest::sign_text(k, garbage), {k.pub}, 0);
    PB_CHECK(has_rule(m, "E-CAT-02"));
    PB_CHECK(!has_rule(m, "E-CAT-01"));
}

PB_TEST(either_key_of_a_two_key_trust_list_verifies) {
    auto current = pbtest::make_keypair(), next = pbtest::make_keypair();
    Signed a = make(current, 10), b = make(next, 11);
    std::vector<std::string> trusted = {current.pub, next.pub};
    PB_CHECK(verify_catalog(a.bytes, a.sig, trusted, 0).ok());
    PB_CHECK(verify_catalog(b.bytes, b.sig, trusted, 0).ok());
    PB_CHECK(has_rule(verify_catalog(b.bytes, b.sig, {current.pub}, 0), "E-CAT-01"));   // old store, new key
}

PB_TEST(signature_text_is_parsed_strictly) {
    auto k = pbtest::make_keypair();
    Signed s = make(k, 10);
    PB_CHECK(verify_catalog(s.bytes, s.sig, {k.pub}, 0).ok());                      // with trailing newline
    std::string no_nl = s.sig.substr(0, s.sig.size() - 1);
    PB_CHECK(verify_catalog(s.bytes, no_nl, {k.pub}, 0).ok());                      // without
    std::string spaced = no_nl;
    spaced.insert(10, " ");
    PB_CHECK(has_rule(verify_catalog(s.bytes, spaced, {k.pub}, 0), "E-CAT-01"));    // embedded space
    PB_CHECK(has_rule(verify_catalog(s.bytes, no_nl + "\n\n", {k.pub}, 0), "E-CAT-01"));
    PB_CHECK(has_rule(verify_catalog(s.bytes, "", {k.pub}, 0), "E-CAT-01"));
    PB_CHECK(has_rule(verify_catalog(s.bytes, "not base64!!", {k.pub}, 0), "E-CAT-01"));
}

PB_TEST(public_key_text_is_one_line_of_base64) {
    auto k = pbtest::make_keypair();
    auto parsed = parse_public_key_text(k.pub_text());
    PB_CHECK(parsed.has_value());
    PB_CHECK_EQ(*parsed, k.pub);
    PB_CHECK(!parse_public_key_text("").has_value());
    PB_CHECK(!parse_public_key_text("AAAA\n").has_value());                // wrong length
    PB_CHECK(!parse_public_key_text(k.pub_text() + "extra\n").has_value());
}

PB_TEST_MAIN()
