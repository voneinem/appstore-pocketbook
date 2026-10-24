// T048: HTTPS download rules against a local TLS server (certificate verification is always on).
#include "pbstore/fetch.h"
#include "test_support.h"
#include "tls_test_server.h"
#include "tmp_dir.h"

#include <fstream>
#include <sstream>
#include <thread>

using namespace pbstore;

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

namespace {
struct Harness {
    pbtest::TmpDir dir;
    pbtest::TestPki pki{dir};
    pbtest::TestPki::Leaf leaf;
    Harness() { leaf = pki.issue("srv", "DNS:localhost,IP:127.0.0.1"); }
    bool ready() const { return pki.available() && leaf.ok; }
};
bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }
}  // namespace

PB_TEST(a_download_over_https_with_a_trusted_certificate_succeeds) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [](const std::string&) { return pbtest::TlsTestServer::http_response(200, "package-bytes"); });
    PB_CHECK(srv.running());
    FetchOptions o;
    o.ca_file = h.pki.ca_cert();
    FetchResult r = https_get(srv.url("/pkg.zip"), o);
    PB_CHECK(r.ok);
    PB_CHECK_EQ(r.http_status, 200L);
    PB_CHECK_EQ(r.body, std::string("package-bytes"));
    PB_CHECK(has(srv.requests().at(0), "GET /pkg.zip"));
    PB_CHECK(has(srv.requests().at(0), "pbstore-catalog"));   // a plain user agent, nothing else identifying
    PB_CHECK(!srv.saw_authorization());
}

PB_TEST(an_untrusted_certificate_fails_and_cannot_be_overridden) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [](const std::string&) { return pbtest::TlsTestServer::http_response(200, "secret"); });
    // 1. the system store does not know the test CA
    FetchResult a = https_get(srv.url("/"), FetchOptions());
    PB_CHECK(!a.ok);
    PB_CHECK(a.kind == FetchKind::Tls);
    PB_CHECK(a.body.empty());
    PB_CHECK(has(a.error, "certificate"));
    // 2. another CA is trusted, not this one
    pbtest::TmpDir other_dir;
    pbtest::TestPki other(other_dir, "other");
    FetchOptions o;
    o.ca_file = other.ca_cert();
    FetchResult b = https_get(srv.url("/"), o);
    PB_CHECK(!b.ok && b.kind == FetchKind::Tls && b.body.empty());
    // 3. a self-signed certificate is not trusted unless it is the given anchor
    auto self = h.pki.self_signed("selfsigned", "DNS:localhost,IP:127.0.0.1");
    pbtest::TlsTestServer srv2(self.cert, self.key, [](const std::string&) { return pbtest::TlsTestServer::http_response(200, "secret"); });
    FetchOptions o2;
    o2.ca_file = h.pki.ca_cert();
    PB_CHECK(!https_get(srv2.url("/"), o2).ok);
    // the server never saw a request: nothing was sent over an unverified connection
    PB_CHECK(srv.requests().empty());
    PB_CHECK(srv2.requests().empty());
}

PB_TEST(plain_http_is_refused_before_any_connection) {
    FetchResult r = https_get("http://localhost:1/pkg.zip", FetchOptions());
    PB_CHECK(!r.ok);
    PB_CHECK(r.kind == FetchKind::Protocol);
    PB_CHECK(has(r.error, "https"));
    for (const char* bad : {"ftp://example.com/x", "file:///etc/passwd", "gopher://x", "example.com/x", ""}) {
        FetchResult x = https_get(bad, FetchOptions());
        PB_CHECK(!x.ok && x.kind == FetchKind::Protocol);
    }
}

PB_TEST(a_redirect_to_http_is_refused) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [](const std::string&) {
        return pbtest::TlsTestServer::http_response(302, "", "Location: http://localhost:1/downgraded\r\n");
    });
    FetchOptions o;
    o.ca_file = h.pki.ca_cert();
    FetchResult r = https_get(srv.url("/"), o);
    PB_CHECK(!r.ok);
    PB_CHECK(r.kind == FetchKind::Protocol);
    PB_CHECK(has(r.error, "redirect") && has(r.error, "https"));
}

PB_TEST(more_than_five_redirects_are_refused_and_up_to_five_are_followed) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    const int port_holder = 0;
    (void)port_holder;
    std::atomic<int> total{0};
    pbtest::TlsTestServer* self = nullptr;
    // /r/N redirects to /r/(N-1); /r/0 answers
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [&](const std::string& req) {
        ++total;
        const size_t p = req.find("GET /r/");
        const int n = p == std::string::npos ? 0 : std::atoi(req.c_str() + p + 7);
        if (n <= 0) return pbtest::TlsTestServer::http_response(200, "arrived");
        return pbtest::TlsTestServer::http_response(302, "", "Location: https://localhost:" + std::to_string(self->port()) + "/r/" + std::to_string(n - 1) + "\r\n");
    });
    self = &srv;
    FetchOptions o;
    o.ca_file = h.pki.ca_cert();
    FetchResult ok = https_get(srv.url("/r/5"), o);
    PB_CHECK(ok.ok);
    PB_CHECK_EQ(ok.body, std::string("arrived"));
    FetchResult many = https_get(srv.url("/r/6"), o);
    PB_CHECK(!many.ok);
    PB_CHECK(has(many.error, "redirect"));
}

PB_TEST(a_body_larger_than_the_limit_is_aborted_at_the_limit) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [](const std::string&) {
        return pbtest::TlsTestServer::http_response(200, std::string(200000, 'x'));
    });
    FetchOptions o;
    o.ca_file = h.pki.ca_cert();
    o.max_bytes = 1000;
    FetchResult r = https_get(srv.url("/big"), o);
    PB_CHECK(!r.ok);
    PB_CHECK(r.kind == FetchKind::TooLarge);
    PB_CHECK(r.body.size() <= 1000);
    PB_CHECK(has(r.error, "larger") && has(r.error, "1000"));
    o.max_bytes = 300000;
    PB_CHECK(https_get(srv.url("/big"), o).ok);
}

PB_TEST(http_errors_and_timeouts_name_their_cause) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [](const std::string& req) {
        if (req.find("GET /slow") != std::string::npos) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            return pbtest::TlsTestServer::http_response(200, "late");
        }
        return pbtest::TlsTestServer::http_response(404, "nope");
    });
    FetchOptions o;
    o.ca_file = h.pki.ca_cert();
    FetchResult nf = https_get(srv.url("/missing"), o);
    PB_CHECK(!nf.ok && nf.kind == FetchKind::HttpStatus && nf.http_status == 404);
    PB_CHECK(has(nf.error, "404"));
    o.timeout_seconds = 1;
    FetchResult slow = https_get(srv.url("/slow"), o);
    PB_CHECK(!slow.ok && slow.kind == FetchKind::Timeout);
    PB_CHECK(has(slow.error, "timed out"));
    FetchResult gone = https_get("https://localhost:1/", FetchOptions());
    PB_CHECK(!gone.ok && gone.kind == FetchKind::Unreachable);
    PB_CHECK(has(gone.error, "reach") || has(gone.error, "connect"));
}

PB_TEST(verification_cannot_be_switched_off_in_the_source) {
    const std::string cpp = slurp(std::string(PBSTORE_SOURCE_DIR) + "/core/src/fetch.cpp");
    const std::string hdr = slurp(std::string(PBSTORE_SOURCE_DIR) + "/core/include/pbstore/fetch.h");
    PB_CHECK(!cpp.empty() && !hdr.empty());
    PB_CHECK(has(cpp, "CURLOPT_SSL_VERIFYPEER, 1L"));
    PB_CHECK(has(cpp, "CURLOPT_SSL_VERIFYHOST, 2L"));
    for (const char* bad : {"VERIFYPEER, 0", "VERIFYHOST, 0", "VERIFYHOST, 1", "VERIFYSTATUS", "SSL_VERIFYPEER,0"})
        PB_CHECK(!has(cpp, bad));
    PB_CHECK(has(cpp, "CURLOPT_PROTOCOLS_STR, \"https\""));
    PB_CHECK(has(cpp, "CURLOPT_REDIR_PROTOCOLS_STR, \"https\""));
    PB_CHECK(!has(cpp, "getenv"));   // no environment variable can change the TLS behavior
    for (const char* bad : {"insecure", "skip_verify", "verify_peer", "no_verify"}) PB_CHECK(!has(hdr, bad));
}

PB_TEST_MAIN()
