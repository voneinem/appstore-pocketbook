// T062: the installer's catalog session against a local HTTPS server with certificate verification on:
// signature, rollback, schema, size, and a distinct plain message for every way the network can fail.
#include "install_rig.h"
#include "pbstore/install/catalog_session.h"
#include "pbstore/install/package_pipeline.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

struct Rig {
    KeyPair key = make_keypair();
    CatalogHost host{key};
    MemStateDirs state;
    StateStore store{state};
    Rig() {
        if (host.ready()) {
            host.add_app(nested_sample_app());
            host.publish(100);
        }
    }
    CatalogSession refresh(const Downloader& dl = nullptr, const std::vector<std::string>& keys = {}) {
        return refresh_catalog(dl ? dl : host.downloader(), store, host.catalog_url(), keys.empty() ? std::vector<std::string>{key.pub} : keys);
    }
};

}  // namespace

PB_TEST(a_genuine_catalog_is_accepted_its_date_kept_and_its_serial_remembered) {
    Rig r;
    if (!r.host.ready()) PB_SKIP("openssl CLI not available");
    CatalogSession s = r.refresh();
    PB_CHECK(s.findings.empty());
    PB_CHECK(s.catalog.has_value());
    PB_CHECK_EQ(s.catalog->generated_at, std::string("2026-10-24T12:00:00Z"));
    PB_CHECK_EQ(r.store.last_serial().value_or(0), int64_t(100));
    PB_CHECK(has_rule(s.notices, "W-INS-03"));       // a first run: no earlier serial
    CatalogSession again = r.refresh();
    PB_CHECK(again.catalog.has_value());             // an equal serial is accepted as unchanged
    PB_CHECK(!has_rule(again.notices, "W-INS-03"));
}

PB_TEST(changed_content_or_a_wrong_signature_is_refused_with_e_cat_01_and_the_serial_stays) {
    Rig r;
    if (!r.host.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.refresh().catalog.has_value());
    r.host.publish(101);
    r.host.files["/catalog.json"] += " ";            // one byte more than what was signed
    CatalogSession a = r.refresh();
    PB_CHECK(!a.catalog.has_value());
    PB_CHECK(has_rule(a.findings, "E-CAT-01"));
    PB_CHECK_EQ(r.store.last_serial().value_or(0), int64_t(100));
    r.host.publish(102);
    KeyPair other = make_keypair();
    CatalogSession b = r.refresh(nullptr, {other.pub});   // signed by a key we do not trust
    PB_CHECK(!b.catalog.has_value());
    PB_CHECK(has_rule(b.findings, "E-CAT-01"));
    PB_CHECK_EQ(r.store.last_serial().value_or(0), int64_t(100));
}

PB_TEST(a_lower_serial_is_refused_as_a_rollback_and_the_remembered_serial_stays) {
    Rig r;
    if (!r.host.ready()) PB_SKIP("openssl CLI not available");
    r.host.publish(200);
    PB_CHECK(r.refresh().catalog.has_value());
    r.host.publish(150);
    CatalogSession s = r.refresh();
    PB_CHECK(!s.catalog.has_value());
    PB_CHECK(has_rule(s.findings, "E-CAT-04"));
    PB_CHECK_EQ(r.store.last_serial().value_or(0), int64_t(200));
}

PB_TEST(a_newer_major_schema_says_the_program_needs_updating) {
    Rig r;
    if (!r.host.ready()) PB_SKIP("openssl CLI not available");
    r.host.catalog.schema_version = "2.0";
    r.host.publish(300);
    CatalogSession s = r.refresh();
    PB_CHECK(!s.catalog.has_value());
    PB_CHECK(has_rule(s.findings, "E-CAT-03"));
    PB_CHECK(r.store.last_serial().value_or(0) != 300);
}

PB_TEST(a_catalog_larger_than_one_mib_is_refused_with_e_cat_05) {
    Rig r;
    if (!r.host.ready()) PB_SKIP("openssl CLI not available");
    std::string big(1024 * 1024 + 10, 'x');
    r.host.set_raw("/catalog.json", big);
    r.host.set_raw("/catalog.json.sig", sign_text(r.key, big));
    CatalogSession s = r.refresh();
    PB_CHECK(!s.catalog.has_value());
    PB_CHECK(has_rule(s.findings, "E-CAT-05"));
}

PB_TEST(every_way_the_network_can_fail_has_its_own_plain_sentence_and_none_suggests_turning_verification_off) {
    std::vector<std::pair<std::string, Finding>> cases;
    auto add = [&](const std::string& name, const CatalogSession& s) {
        PB_CHECK(!s.catalog.has_value());
        PB_CHECK(!s.findings.empty());
        PB_CHECK(s.findings[0].rule_id == "E-INS-26" || s.findings[0].rule_id == "E-INS-36");
        cases.push_back({name, s.findings[0]});
    };
    {   // nobody listens
        Rig r;
        if (!r.host.ready()) PB_SKIP("openssl CLI not available");
        MemStateDirs st; StateStore store(st);
        add("unreachable", refresh_catalog(r.host.downloader(), store, "https://localhost:1/catalog.json", {r.key.pub}));
    }
    {   // a CA the program does not trust
        Rig r;
        if (!r.host.ready()) PB_SKIP("openssl CLI not available");
        TmpDir d;
        TestPki other(d, "other");
        add("untrusted CA", r.refresh(r.host.downloader("pbstore-install/0.2.0", 30, other.ca_cert())));
    }
    {   // a certificate for another host name
        KeyPair k = make_keypair();
        CatalogHost wrong(k, "DNS:wrong.example,IP:10.9.9.9");
        if (!wrong.ready()) PB_SKIP("openssl CLI not available");
        wrong.publish(100);
        MemStateDirs st; StateStore store(st);
        add("wrong host name", refresh_catalog(wrong.downloader(), store, wrong.catalog_url(), {k.pub}));
    }
    {   // an expired certificate
        TmpDir d;
        TestPki pki(d);
        if (!pki.available()) PB_SKIP("openssl CLI not available");
        auto leaf = pki.issue("old", "DNS:localhost,IP:127.0.0.1", "20200101000000Z", "20200102000000Z");
        TlsTestServer srv(leaf.cert, leaf.key, [](const std::string&) { return TlsTestServer::http_response(200, "x"); });
        NetOptions o; o.ca_file = pki.ca_cert(); o.user_agent = "pbstore-install/0.2.0";
        MemStateDirs st; StateStore store(st);
        add("expired", refresh_catalog(make_https_downloader(o), store, srv.url("/catalog.json"), {make_keypair().pub}));
    }
    {   // a redirect to plain HTTP
        Rig r;
        if (!r.host.ready()) PB_SKIP("openssl CLI not available");
        r.host.redirect_path = "/catalog.json";
        r.host.redirect_to = "http://localhost:1/catalog.json";
        add("redirect to http", r.refresh());
    }
    {   // a transfer that stalls
        Rig r;
        if (!r.host.ready()) PB_SKIP("openssl CLI not available");
        r.host.stall_path = "/catalog.json";
        r.host.stall_ms = 3500;
        add("stall", r.refresh(r.host.downloader("pbstore-install/0.2.0", /*stall_seconds=*/1)));
    }
    {   // an error status
        Rig r;
        if (!r.host.ready()) PB_SKIP("openssl CLI not available");
        r.host.files.erase("/catalog.json");
        add("status 404", r.refresh());
    }
    PB_CHECK(cases.size() >= 7);
    std::set<std::string> sentences;
    for (const auto& c : cases) {
        const std::string& m = c.second.message;
        if (!sentences.insert(m).second) PB_FAIL_AT("two failures have the same sentence: " + c.first + ": " + m);
        for (const char* bad : {"insecure", "disable", "turn off", "skip", "ignore the", "-k", "no-verify", "no verification", "bypass"})
            if (has(m, bad)) PB_FAIL_AT(c.first + ": the message suggests weakening the check: " + m);
    }
    for (const auto& c : cases) {
        if (c.first == "untrusted CA" || c.first == "wrong host name" || c.first == "expired") {
            if (!has(c.second.message, "clock")) PB_FAIL_AT(c.first + ": a certificate error should name the computer's clock: " + c.second.message);
            if (!has(c.second.message, "certificate")) PB_FAIL_AT(c.first + ": should say certificate: " + c.second.message);
        }
        if (c.first == "stall") PB_CHECK_EQ(c.second.rule_id, std::string("E-INS-36"));
    }
}

PB_TEST(the_environment_cannot_add_trust) {
    Rig r;
    if (!r.host.ready()) PB_SKIP("openssl CLI not available");
    TmpDir d;
    TestPki other(d, "other");
    ::setenv("SSL_CERT_FILE", r.host.ca_file().c_str(), 1);
    ::setenv("SSL_CERT_DIR", d.path().c_str(), 1);
    CatalogSession s = r.refresh(r.host.downloader("pbstore-install/0.2.0", 30, other.ca_cert()));
    ::unsetenv("SSL_CERT_FILE");
    ::unsetenv("SSL_CERT_DIR");
    PB_CHECK(!s.catalog.has_value());
    PB_CHECK(has_rule(s.findings, "E-INS-26"));
}

PB_TEST(a_package_is_fetched_from_the_signed_address_with_the_signed_size_as_the_limit) {
    Rig r;
    if (!r.host.ready()) PB_SKIP("openssl CLI not available");
    CatalogSession s = r.refresh();
    const CatalogEntry entry = s.catalog->apps[0];
    Package ok = fetch_catalog_package(r.host.downloader(), entry, nullptr);
    PB_CHECK(ok.ok());
    PB_CHECK(r.host.request_paths().back() == package_path_of(entry));
    // the server answers with more than the signed size: the download is stopped at the limit
    r.host.files[package_path_of(entry)] += std::string(5000, 'x');
    Package big = fetch_catalog_package(r.host.downloader(), entry, nullptr);
    PB_CHECK(!big.ok());
    PB_CHECK(has_rule(big.findings, "E-INS-26"));
    PB_CHECK(has(big.findings[0].message, "larger"));
    // a package of another size: refused by the entry check
    std::string shorter = r.host.files[package_path_of(entry)].substr(0, entry.package_size - 10);
    r.host.files[package_path_of(entry)] = shorter;
    Package small = fetch_catalog_package(r.host.downloader(), entry, nullptr);
    PB_CHECK(!small.ok());
    PB_CHECK(has_rule(small.findings, "E-ENT-15"));
}

PB_TEST(the_request_names_only_the_program_and_nothing_else_is_ever_requested) {
    Rig r;
    if (!r.host.ready()) PB_SKIP("openssl CLI not available");
    CatalogSession s = r.refresh();
    PB_CHECK(s.catalog.has_value());
    fetch_catalog_package(r.host.downloader(), s.catalog->apps[0], nullptr);
    for (const std::string& req : r.host.raw_requests()) {
        PB_CHECK(has(req, "User-Agent: pbstore-install/0.2.0\r\n"));
        std::string low = req;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* header : {"authorization:", "cookie:", "x-", "referer:", "proxy-"}) PB_CHECK(!has(low, std::string("\r\n") + header));
    }
    // the addresses in the catalog for source, changelog and icon are never requested
    for (const std::string& path : r.host.request_paths()) {
        PB_CHECK(path == "/catalog.json" || path == "/catalog.json.sig" || path == package_path_of(s.catalog->apps[0]));
    }
}

PB_TEST_MAIN()
