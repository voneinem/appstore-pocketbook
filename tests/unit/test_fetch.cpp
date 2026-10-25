// T048: HTTPS download rules against a local TLS server (certificate verification is always on).
#include "pbstore/fetch.h"
#include "test_support.h"
#include "tls_test_server.h"
#include "tmp_dir.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

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

// ---- additive options for the installer (feature 002, T008) ------------------------------------------
namespace {
std::string pem_of(const std::string& path) { return slurp(path); }

// A response that arrives in `pieces` pieces, one per `gap_ms` (a slow download).
pbtest::TlsTestServer::Streamer slow_body(int pieces, int gap_ms, size_t piece_size = 1000) {
    return [=](const std::string&, const std::function<bool(const std::string&)>& write) {
        const size_t total = static_cast<size_t>(pieces) * piece_size;
        write("HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(total) + "\r\nConnection: close\r\n\r\n");
        for (int i = 0; i < pieces; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(gap_ms));
            if (!write(std::string(piece_size, 'x'))) return;
        }
    };
}
}  // namespace

PB_TEST(the_user_agent_is_sent_as_given_and_defaults_to_the_catalog_tool) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [](const std::string&) { return pbtest::TlsTestServer::http_response(200, "ok"); });
    FetchOptions o;
    o.ca_file = h.pki.ca_cert();
    o.user_agent = "pbstore-install/9.9.9";
    PB_CHECK(https_get(srv.url("/"), o).ok);
    PB_CHECK(has(srv.requests().at(0), "User-Agent: pbstore-install/9.9.9\r\n"));
    PB_CHECK(!has(srv.requests().at(0), "pbstore-catalog"));
    FetchOptions d;
    d.ca_file = h.pki.ca_cert();
    PB_CHECK(https_get(srv.url("/"), d).ok);
    PB_CHECK(has(srv.requests().at(1), "User-Agent: pbstore-catalog/"));
}

PB_TEST(progress_is_reported_at_least_once_a_second_during_a_slow_transfer) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, nullptr, slow_body(4, 700));
    std::vector<std::pair<uint64_t, uint64_t>> seen;
    std::vector<std::chrono::steady_clock::time_point> when;
    FetchOptions o;
    o.ca_file = h.pki.ca_cert();
    o.progress = [&](uint64_t done, uint64_t total) {
        seen.push_back({done, total});
        when.push_back(std::chrono::steady_clock::now());
    };
    FetchResult r = https_get(srv.url("/slow"), o);
    PB_CHECK(r.ok);
    PB_CHECK_EQ(r.body.size(), static_cast<size_t>(4000));
    PB_CHECK(seen.size() >= 3);
    for (size_t i = 1; i < seen.size(); ++i) PB_CHECK(seen[i].first >= seen[i - 1].first);
    PB_CHECK_EQ(seen.back().first, static_cast<uint64_t>(4000));
    PB_CHECK_EQ(seen.back().second, static_cast<uint64_t>(4000));
    // never a silent gap of a second or more in the middle of the transfer
    for (size_t i = 1; i < when.size(); ++i)
        PB_CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(when[i] - when[i - 1]).count() < 1000);
}

PB_TEST(a_transfer_that_receives_no_data_for_the_stall_time_ends_naming_no_data) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, nullptr,
        [](const std::string&, const std::function<bool(const std::string&)>& write) {
            write("HTTP/1.1 200 OK\r\nContent-Length: 100000\r\nConnection: close\r\n\r\n");
            write(std::string(100, 'x'));
            std::this_thread::sleep_for(std::chrono::milliseconds(4500));   // then nothing
        });
    FetchOptions o;
    o.ca_file = h.pki.ca_cert();
    o.stall_seconds = 2;
    o.timeout_seconds = 60;
    const auto t0 = std::chrono::steady_clock::now();
    FetchResult r = https_get(srv.url("/stall"), o);
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
    PB_CHECK(!r.ok);
    PB_CHECK(r.kind == FetchKind::Timeout);
    PB_CHECK(has(r.error, "no data"));
    PB_CHECK(secs < 4);
}

PB_TEST(a_long_transfer_that_keeps_receiving_data_completes_when_the_total_time_allows_it) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    // the default total time of the catalog tools stays 120 seconds
    PB_CHECK_EQ(FetchOptions().timeout_seconds, 120L);
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, nullptr, slow_body(5, 700));   // about 3.5 s in total
    FetchOptions shorter;
    shorter.ca_file = h.pki.ca_cert();
    shorter.timeout_seconds = 2;
    FetchResult cut = https_get(srv.url("/long"), shorter);
    PB_CHECK(!cut.ok && cut.kind == FetchKind::Timeout);
    PB_CHECK(has(cut.error, "timed out"));
    FetchOptions longer;                    // the installer's package download: a long total time ...
    longer.ca_file = h.pki.ca_cert();
    longer.timeout_seconds = 30;            // (600 in the installer; scaled down here)
    longer.stall_seconds = 2;               // ... and a short limit for silence
    FetchResult ok = https_get(srv.url("/long"), longer);
    PB_CHECK(ok.ok);
    PB_CHECK_EQ(ok.body.size(), static_cast<size_t>(5000));
}

PB_TEST(a_given_ca_blob_replaces_the_system_store_and_the_environment_changes_nothing) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [](const std::string&) { return pbtest::TlsTestServer::http_response(200, "ok"); });
    FetchOptions good;
    good.ca_blob = pem_of(h.pki.ca_cert());
    FetchResult a = https_get(srv.url("/"), good);
    PB_CHECK(a.ok);
    PB_CHECK_EQ(a.body, std::string("ok"));
    // another CA as the blob, the test CA only in the environment: the environment is not consulted
    pbtest::TmpDir other_dir;
    pbtest::TestPki other(other_dir, "other");
    FetchOptions wrong;
    wrong.ca_blob = pem_of(other.ca_cert());
    ::setenv("SSL_CERT_FILE", h.pki.ca_cert().c_str(), 1);
    ::setenv("SSL_CERT_DIR", h.dir.path().c_str(), 1);
    FetchResult b = https_get(srv.url("/"), wrong);
    ::unsetenv("SSL_CERT_FILE");
    ::unsetenv("SSL_CERT_DIR");
    PB_CHECK(!b.ok);
    PB_CHECK(b.kind == FetchKind::Tls);
}

PB_TEST(verification_stays_on_for_every_combination_of_the_new_options) {
    Harness h;
    if (!h.ready()) PB_SKIP("openssl CLI not available");
    pbtest::TlsTestServer srv(h.leaf.cert, h.leaf.key, [](const std::string&) { return pbtest::TlsTestServer::http_response(200, "secret"); });
    pbtest::TmpDir other_dir;
    pbtest::TestPki other(other_dir, "other");
    for (int mask = 0; mask < 16; ++mask) {
        FetchOptions o;
        if (mask & 1) o.user_agent = "pbstore-install/1";
        if (mask & 2) o.progress = [](uint64_t, uint64_t) {};
        if (mask & 4) o.stall_seconds = 5;
        if (mask & 8) o.ca_blob = pem_of(other.ca_cert()); else o.ca_file = other.ca_cert();
        FetchResult r = https_get(srv.url("/"), o);
        PB_CHECK(!r.ok);
        PB_CHECK(r.kind == FetchKind::Tls);
        PB_CHECK(r.body.empty());
    }
    PB_CHECK(srv.requests().empty());
}

PB_TEST_MAIN()
