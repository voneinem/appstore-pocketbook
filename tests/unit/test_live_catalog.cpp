// The shared loader of the live catalog (check-entry and build): verified like a consumer would.
#include "pbstore/fetch.h"
#include "pbstore/live_catalog.h"
#include "keys.h"
#include "sample_catalog.h"
#include "test_support.h"
#include "tls_test_server.h"
#include "tmp_dir.h"

using namespace pbstore;

static bool has(const LiveCatalogResult& r, const char* rule, const char* needle = "") {
    for (const Finding& f : r.findings)
        if (f.rule_id == rule && f.message.find(needle) != std::string::npos) return true;
    return false;
}

static void write_live(const pbtest::TmpDir& t, const pbtest::KeyPair& k, int64_t serial) {
    const std::string bytes = write_catalog(pbtest::sample_catalog(serial));
    t.write("catalog.json", bytes);
    t.write("catalog.json.sig", pbtest::sign_text(k, bytes));
}

PB_TEST(a_verified_live_catalog_is_loaded_from_a_directory) {
    pbtest::TmpDir t;
    auto k = pbtest::make_keypair();
    write_live(t, k, 777);
    LiveCatalogResult r = load_live_catalog_dir(t.path(), {k.pub});
    PB_CHECK(r.catalog.has_value());
    PB_CHECK(r.findings.empty());
    PB_CHECK_EQ(r.catalog->serial, int64_t(777));
}

PB_TEST(a_live_catalog_that_cannot_be_used_is_E_BLD_03_and_names_the_cause) {
    pbtest::TmpDir t;
    auto k = pbtest::make_keypair(), other = pbtest::make_keypair();
    PB_CHECK(has(load_live_catalog_dir(t.file("nothere"), {k.pub}), "E-BLD-03", "not found"));
    PB_CHECK(has(load_live_catalog_dir(t.path(), {k.pub}), "E-BLD-03", "catalog.json"));        // files missing
    write_live(t, k, 5);
    PB_CHECK(has(load_live_catalog_dir(t.path(), {other.pub}), "E-BLD-03", "E-CAT-01"));         // untrusted key
    std::string bytes = t.read("catalog.json");
    bytes[bytes.find("alpha-app")] = 'z';
    t.write("catalog.json", bytes);
    LiveCatalogResult tampered = load_live_catalog_dir(t.path(), {k.pub});
    PB_CHECK(!tampered.catalog.has_value() && has(tampered, "E-BLD-03", "signature"));
    ::unlink(t.file("catalog.json.sig").c_str());
    PB_CHECK(has(load_live_catalog_dir(t.path(), {k.pub}), "E-BLD-03", "catalog.json.sig"));
}

static Fetcher serving_files(std::string catalog, std::string sig, int* calls = nullptr) {
    return [=](const std::string& url, uint64_t) {
        if (calls) ++*calls;
        FetchResult r;
        r.ok = true;
        r.http_status = 200;
        r.body = url.size() > 4 && url.compare(url.size() - 4, 4, ".sig") == 0 ? sig : catalog;
        return r;
    };
}

PB_TEST(the_live_catalog_can_be_loaded_over_https_and_is_verified) {
    auto k = pbtest::make_keypair(), other = pbtest::make_keypair();
    const std::string bytes = write_catalog(pbtest::sample_catalog(900));
    int calls = 0;
    LiveCatalogResult ok = load_live_catalog_url("https://example.github.io/pb/catalog.json", {k.pub},
                                                 serving_files(bytes, pbtest::sign_text(k, bytes), &calls));
    PB_CHECK(ok.catalog.has_value() && ok.catalog->serial == 900);
    PB_CHECK_EQ(calls, 2);                                      // catalog.json and catalog.json.sig
    LiveCatalogResult wrong = load_live_catalog_url("https://example.github.io/pb/catalog.json", {other.pub},
                                                    serving_files(bytes, pbtest::sign_text(k, bytes)));
    PB_CHECK(!wrong.catalog.has_value() && has(wrong, "E-BLD-03", "E-CAT-01"));
    Fetcher down = [](const std::string&, uint64_t) { FetchResult r; r.kind = FetchKind::HttpStatus; r.http_status = 404; r.error = "the server answered with status 404"; return r; };
    LiveCatalogResult gone = load_live_catalog_url("https://example.github.io/pb/catalog.json", {k.pub}, down);
    PB_CHECK(!gone.catalog.has_value() && has(gone, "E-BLD-03", "404"));
    PB_CHECK(has(load_live_catalog_url("http://example.com/catalog.json", {k.pub}, serving_files(bytes, "")), "E-BLD-03", "https"));
}

PB_TEST(the_real_https_fetcher_loads_a_live_catalog_from_a_local_server) {
    pbtest::TmpDir t;
    pbtest::TestPki pki(t);
    if (!pki.available()) PB_SKIP("openssl CLI not available");
    auto leaf = pki.issue("srv", "DNS:localhost,IP:127.0.0.1");
    auto k = pbtest::make_keypair();
    const std::string bytes = write_catalog(pbtest::sample_catalog(321));
    const std::string sig = pbtest::sign_text(k, bytes);
    pbtest::TlsTestServer srv(leaf.cert, leaf.key, [&](const std::string& req) {
        if (req.find("GET /catalog.json.sig") != std::string::npos) return pbtest::TlsTestServer::http_response(200, sig);
        if (req.find("GET /catalog.json") != std::string::npos) return pbtest::TlsTestServer::http_response(200, bytes);
        return pbtest::TlsTestServer::http_response(404, "");
    });
    FetchOptions o;
    o.ca_file = pki.ca_cert();
    LiveCatalogResult r = load_live_catalog_url(srv.url("/catalog.json"), {k.pub}, make_https_fetcher(o));
    PB_CHECK(r.catalog.has_value());
    PB_CHECK_EQ(r.catalog->serial, int64_t(321));
    // without the test CA the same server is not trusted
    LiveCatalogResult untrusted = load_live_catalog_url(srv.url("/catalog.json"), {k.pub}, make_https_fetcher(FetchOptions()));
    PB_CHECK(!untrusted.catalog.has_value() && has(untrusted, "E-BLD-03", "certificate"));
}

PB_TEST_MAIN()
