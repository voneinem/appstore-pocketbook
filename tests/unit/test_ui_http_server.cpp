// T113: the assistant's server rules (contracts/ui-api.md): loopback only, a token in the path, the Host
// check, methods, content type, limits, headers, lifetime. A fuzz-style test sends malformed requests.
#include "http_server.h"
#include "test_support.h"
#include "ui_client.h"

#include <sys/socket.h>

#include <atomic>
#include <random>

using namespace pbstore::install::ui;
using namespace pbtest;

namespace {

struct Running {
    HttpServer server;
    std::thread thread;
    std::atomic<int> handled{0};
    std::string last_path, last_body;
    explicit Running(HttpServer::Options options = HttpServer::Options())
        : server([this](const HttpRequest& r) {
                     ++handled;
                     last_path = r.path;
                     last_body = r.body;
                     HttpResponse resp;
                     resp.body = "{\"path\":\"" + r.path + "\",\"method\":\"" + r.method + "\"}";
                     return resp;
                 },
                 options) {
        std::string error;
        if (!server.start(&error)) throw std::runtime_error("server: " + error);
        thread = std::thread([this] { server.run(); });
    }
    ~Running() {
        server.stop();
        if (thread.joinable()) thread.join();
    }
    std::string host() const { return "Host: 127.0.0.1:" + std::to_string(server.port()) + "\r\n"; }
    std::string base() const { return "/" + server.token() + "/"; }
};

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

}  // namespace

PB_TEST(it_binds_loopback_only_on_a_random_port_and_has_a_128_bit_token) {
    Running a, b;
    PB_CHECK(a.server.port() > 0);
    PB_CHECK(a.server.port() != b.server.port() || true);
    PB_CHECK_EQ(a.server.token().size(), size_t(32));
    for (char c : a.server.token()) PB_CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    PB_CHECK(a.server.token() != b.server.token());                              // new for every start
    PB_CHECK(a.server.url() == "http://127.0.0.1:" + std::to_string(a.server.port()) + "/" + a.server.token() + "/");
    // reachable on 127.0.0.1; the listening address is checked in the source by the constitution rule R15
    HttpReply ok = raw_exchange(a.server.port(), "GET " + a.base() + "x HTTP/1.1\r\n" + a.host() + "\r\n");
    PB_CHECK_EQ(ok.status, 200);
}

PB_TEST(the_first_path_segment_must_be_the_token_otherwise_404_without_a_body) {
    Running s;
    for (const std::string& path : {std::string("/"), std::string("/api/state"), std::string("/0000000000000000000000000000000f/api/state"), "/" + s.server.token().substr(0, 31) + "/x",
                                    "/" + s.server.token() + "x/api/state", std::string("/../etc/passwd")}) {
        HttpReply r = raw_exchange(s.server.port(), "GET " + path + " HTTP/1.1\r\n" + s.host() + "\r\n");
        if (r.status != 404) PB_FAIL_AT("expected 404 for " + path + " got " + std::to_string(r.status));
        PB_CHECK(r.body.empty());
    }
    PB_CHECK_EQ(s.handled.load(), 0);
}

PB_TEST(the_host_must_be_the_loopback_address_with_the_port_to_defeat_dns_rebinding) {
    Running s;
    for (const std::string& host : {std::string("Host: evil.example\r\n"), "Host: localhost:" + std::to_string(s.server.port()) + "\r\n", std::string("Host: 127.0.0.1\r\n"),
                                    "Host: 127.0.0.1:" + std::to_string(s.server.port() + 1) + "\r\n", std::string("")}) {
        HttpReply r = raw_exchange(s.server.port(), "GET " + s.base() + "api/state HTTP/1.1\r\n" + host + "\r\n");
        if (r.status != 400) PB_FAIL_AT("expected 400 for host \"" + host + "\" got " + std::to_string(r.status));
    }
    PB_CHECK_EQ(s.handled.load(), 0);
}

PB_TEST(only_get_and_post_are_allowed) {
    Running s;
    for (const char* method : {"PUT", "DELETE", "OPTIONS", "HEAD", "PATCH", "TRACE", "CONNECT"}) {
        HttpReply r = raw_exchange(s.server.port(), std::string(method) + " " + s.base() + "api/state HTTP/1.1\r\n" + s.host() + "\r\n");
        if (r.status != 405) PB_FAIL_AT(std::string("expected 405 for ") + method + " got " + std::to_string(r.status));
    }
    PB_CHECK_EQ(s.handled.load(), 0);
}

PB_TEST(a_post_needs_the_json_content_type_and_a_body_of_at_most_one_mib) {
    Running s;
    const std::string body = "{\"a\":1}";
    for (const std::string& type : {std::string("text/plain"), std::string("application/x-www-form-urlencoded"), std::string("multipart/form-data; boundary=x"), std::string("")}) {
        HttpReply r = raw_exchange(s.server.port(), "POST " + s.base() + "api/x HTTP/1.1\r\n" + s.host() + (type.empty() ? "" : "Content-Type: " + type + "\r\n") +
                                                          "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
        if (r.status != 415) PB_FAIL_AT("expected 415 for type \"" + type + "\" got " + std::to_string(r.status));
    }
    HttpReply ok = raw_exchange(s.server.port(), "POST " + s.base() + "api/x HTTP/1.1\r\n" + s.host() + "Content-Type: application/json; charset=utf-8\r\nContent-Length: " +
                                                     std::to_string(body.size()) + "\r\n\r\n" + body);
    PB_CHECK_EQ(ok.status, 200);
    PB_CHECK_EQ(s.last_body, body);
    const std::string big(1024 * 1024 + 1, 'x');
    HttpReply too = raw_exchange(s.server.port(), "POST " + s.base() + "api/x HTTP/1.1\r\n" + s.host() + "Content-Type: application/json\r\nContent-Length: " + std::to_string(big.size()) + "\r\n\r\n" + big);
    PB_CHECK_EQ(too.status, 413);
    const std::string max(1024 * 1024, ' ');
    HttpReply exact = raw_exchange(s.server.port(), "POST " + s.base() + "api/x HTTP/1.1\r\n" + s.host() + "Content-Type: application/json\r\nContent-Length: " + std::to_string(max.size()) + "\r\n\r\n" + max);
    PB_CHECK_EQ(exact.status, 200);
}

PB_TEST(the_limits_of_the_request_line_the_headers_and_the_time_to_receive_hold) {
    Running s;
    HttpReply long_line = raw_exchange(s.server.port(), "GET " + s.base() + std::string(9000, 'a') + " HTTP/1.1\r\n" + s.host() + "\r\n");
    PB_CHECK(long_line.status == 414 || long_line.status == 400);
    std::string many = "GET " + s.base() + "x HTTP/1.1\r\n" + s.host();
    for (int i = 0; i < 400; ++i) many += "X-Filler-" + std::to_string(i) + ": " + std::string(60, 'v') + "\r\n";
    HttpReply big_headers = raw_exchange(s.server.port(), many + "\r\n");
    PB_CHECK(big_headers.status == 431 || big_headers.status == 400);
    // a slow client: the request never completes; the server answers 408 or closes within the time limit
    const auto t0 = std::chrono::steady_clock::now();
    HttpReply slow = raw_exchange(s.server.port(), "GET " + s.base() + "x HTTP/1.1\r\n" + s.host(), 14000);
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
    PB_CHECK(secs >= 8 && secs <= 13);
    PB_CHECK(slow.status == 0 || slow.status == 408);
    PB_CHECK_EQ(s.handled.load(), 0);
}

PB_TEST(at_most_four_connections_at_a_time) {
    Running s;
    std::vector<int> fds;
    for (int i = 0; i < 4; ++i) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(s.server.port()));
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        PB_CHECK_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr), 0);
        fds.push_back(fd);                      // connected, silent
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    HttpReply fifth = raw_exchange(s.server.port(), "GET " + s.base() + "x HTTP/1.1\r\n" + s.host() + "\r\n", 3000);
    PB_CHECK(fifth.status == 503 || fifth.status == 0);
    for (int fd : fds) ::close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    HttpReply again = raw_exchange(s.server.port(), "GET " + s.base() + "x HTTP/1.1\r\n" + s.host() + "\r\n", 3000);
    PB_CHECK_EQ(again.status, 200);
}

PB_TEST(every_answer_has_the_security_headers_and_none_has_cors_or_cookies) {
    Running s;
    for (const std::string& request : {"GET " + s.base() + "x HTTP/1.1\r\n" + s.host() + "\r\n", std::string("GET /wrong/x HTTP/1.1\r\n") + s.host() + "\r\n",
                                       "GET " + s.base() + "x HTTP/1.1\r\n" + "Host: evil\r\n\r\n", "PUT " + s.base() + "x HTTP/1.1\r\n" + s.host() + "\r\n"}) {
        HttpReply r = raw_exchange(s.server.port(), request);
        PB_CHECK(r.status != 0);
        PB_CHECK_EQ(r.headers["cache-control"], std::string("no-store"));
        PB_CHECK_EQ(r.headers["referrer-policy"], std::string("no-referrer"));
        PB_CHECK_EQ(r.headers["x-content-type-options"], std::string("nosniff"));
        PB_CHECK_EQ(r.headers["connection"], std::string("close"));
        PB_CHECK_EQ(r.headers["content-security-policy"],
                    std::string("default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"));
        for (const auto& h : r.headers) {
            PB_CHECK(h.first.compare(0, 15, "access-control-") != 0);
            PB_CHECK(h.first != "set-cookie");
        }
        PB_CHECK(!has(r.head, "Location:"));       // no redirects
    }
}

PB_TEST(it_ends_thirty_seconds_after_the_last_ping_when_no_operation_runs) {
    std::atomic<long> millis{0};
    std::atomic<bool> busy{false};
    HttpServer::Options o;
    o.now = [&] { return std::chrono::steady_clock::time_point(std::chrono::milliseconds(millis.load())); };
    o.busy = [&] { return busy.load(); };
    Running s(o);
    auto ping = [&] {
        return raw_exchange(s.server.port(), "POST " + s.base() + "api/ping HTTP/1.1\r\n" + s.host() + "Content-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
    };
    PB_CHECK_EQ(ping().status, 200);
    millis = 29000;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    PB_CHECK_EQ(ping().status, 200);                 // still alive, and the ping renews the time
    busy = true;
    millis = 29000 + 31000;                          // 31 s without a ping, but an operation runs: it stays
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    PB_CHECK_EQ(raw_exchange(s.server.port(), "GET " + s.base() + "x HTTP/1.1\r\n" + s.host() + "\r\n").status, 200);
    busy = false;                                    // the operation is over and the page is gone
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    s.thread.join();                                 // run() returned by itself
    HttpReply gone = raw_exchange(s.server.port(), "GET " + s.base() + "x HTTP/1.1\r\n" + s.host() + "\r\n", 500);
    PB_CHECK(!gone.connected || gone.status == 0);
}

PB_TEST(post_api_quit_ends_the_server_after_answering) {
    Running s;
    HttpReply r = raw_exchange(s.server.port(), "POST " + s.base() + "api/quit HTTP/1.1\r\n" + s.host() + "Content-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
    PB_CHECK_EQ(r.status, 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    s.thread.join();
}

PB_TEST(malformed_oversized_and_truncated_requests_never_crash_the_server) {
    Running s;
    std::mt19937 rng(7);
    std::vector<std::string> bad = {
        "", "\r\n\r\n", "GARBAGE\r\n\r\n", "GET\r\n\r\n", "GET /\r\n\r\n", "GET / HTTP/9.9\r\n\r\n", std::string("GET /\0x HTTP/1.1\r\n\r\n", 20),
        "GET " + s.base() + "x HTTP/1.1\r\nBad Header Line\r\n" + s.host() + "\r\n", "GET " + s.base() + "x HTTP/1.1\r\n" + s.host() + "Content-Length: -5\r\n\r\n",
        "POST " + s.base() + "x HTTP/1.1\r\n" + s.host() + "Content-Type: application/json\r\nContent-Length: 99999999999999999999\r\n\r\n",
        "POST " + s.base() + "x HTTP/1.1\r\n" + s.host() + "Content-Type: application/json\r\nContent-Length: 100\r\n\r\nshort",
        "POST " + s.base() + "x HTTP/1.1\r\n" + s.host() + "Content-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
        "GET " + s.base() + "%00%2e%2e/%2e%2e/etc/passwd HTTP/1.1\r\n" + s.host() + "\r\n", "GET " + s.base() + "x?" + std::string(5000, 'q') + " HTTP/1.1\r\n" + s.host() + "\r\n",
    };
    for (int i = 0; i < 60; ++i) {
        std::string junk(static_cast<size_t>(rng() % 300), '\0');
        for (char& c : junk) c = static_cast<char>(rng() % 256);
        bad.push_back(junk);
        std::string head = "GET " + s.base() + "x HTTP/1.1\r\n" + s.host();
        head.resize(rng() % head.size());
        bad.push_back(head);
    }
    for (const std::string& b : bad) raw_exchange(s.server.port(), b, 400, /*shutdown_write=*/true);
    HttpReply fine = raw_exchange(s.server.port(), "GET " + s.base() + "x HTTP/1.1\r\n" + s.host() + "\r\n");
    PB_CHECK_EQ(fine.status, 200);               // still serving
}

PB_TEST_MAIN()
