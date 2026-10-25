#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/rand.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

namespace pbstore {
namespace install {
namespace ui {

namespace {

constexpr size_t kMaxRequestLine = 8 * 1024;
constexpr size_t kMaxHeaders = 16 * 1024;
constexpr size_t kMaxBody = 1024 * 1024;
constexpr int kMaxConnections = 4;
constexpr std::chrono::seconds kReceiveTime{10};

const char* kCsp =
    "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

const char* status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Content";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Status";
    }
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex(const unsigned char* data, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 15]);
    }
    return out;
}

bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

void send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

void send_response(int fd, int status, const std::string& content_type, const std::string& body) {
    std::string head = std::string("HTTP/1.1 ") + std::to_string(status) + " " + status_text(status) + "\r\n";
    if (!body.empty()) head += "Content-Type: " + content_type + "\r\n";
    head += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    head += "Connection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\n";
    head += std::string("Content-Security-Policy: ") + kCsp + "\r\n\r\n";
    send_all(fd, head + body);
}

// Reads from the socket until `done` says the data is complete, the deadline passes (false) or the peer closes.
enum class ReadResult { Complete, Timeout, Closed, Error };

bool valid_header_name(const std::string& n) {
    if (n.empty()) return false;
    for (unsigned char c : n)
        if (!(std::isalnum(c) || c == '-' || c == '_')) return false;
    return true;
}

}  // namespace

HttpServer::HttpServer(Handler handler, Options options) : handler_(std::move(handler)), options_(std::move(options)) {}

HttpServer::~HttpServer() {
    stop_ = true;
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

std::string HttpServer::url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/" + token_ + "/"; }

bool HttpServer::start(std::string* error) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        if (error) *error = "cannot make a socket";
        return false;
    }
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // only this computer can connect
    addr.sin_port = 0;
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(listen_fd_, 8) != 0) {
        if (error) *error = "cannot listen on the loopback address";
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    socklen_t len = sizeof addr;
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    unsigned char random[16];
    if (RAND_bytes(random, sizeof random) != 1) {
        if (error) *error = "no random numbers are available";
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    token_ = hex(random, sizeof random);
    started_ = now();
    last_ping_ = started_;
    return true;
}

void HttpServer::run() {
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::vector<Worker> workers;
    while (!stop_) {
        {   // the lifetime: ends a while after the last ping (or the start) unless an operation runs
            std::lock_guard<std::mutex> lock(mu_);
            const auto t = now();
            const bool idle = pinged_ ? (t - last_ping_ > options_.idle_after_ping) : (t - started_ > options_.first_ping_within);
            if (idle && !(options_.busy && options_.busy())) break;
        }
        pollfd p{listen_fd_, POLLIN, 0};
        if (::poll(&p, 1, 50) > 0 && (p.revents & POLLIN)) {
            const int fd = ::accept(listen_fd_, nullptr, nullptr);
            if (fd >= 0) {
                if (active_ >= kMaxConnections) {
                    send_response(fd, 503, "", "");
                    ::close(fd);
                } else {
                    ++active_;
                    auto done = std::make_shared<std::atomic<bool>>(false);
                    workers.push_back({std::thread([this, fd, done] {
                                           serve_connection(fd);
                                           --active_;
                                           *done = true;
                                       }),
                                       done});
                }
            }
        }
        for (size_t i = workers.size(); i-- > 0;)
            if (*workers[i].done) {
                workers[i].thread.join();
                workers.erase(workers.begin() + static_cast<long>(i));
            }
    }
    stop_ = true;
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    for (Worker& w : workers) w.thread.join();
}

HttpResponse HttpServer::respond(const HttpRequest& req) {
    if (req.method == "POST" && req.path == "api/ping") {
        std::lock_guard<std::mutex> lock(mu_);
        pinged_ = true;
        last_ping_ = now();
        HttpResponse r;
        r.body = "{\"ok\":true}";
        return r;
    }
    if (req.method == "POST" && req.path == "api/quit") {
        HttpResponse r;
        r.body = "{\"ok\":true}";
        stop_ = true;   // the answer is sent first: the server loop ends after the connection is served
        return r;
    }
    return handler_(req);
}

void HttpServer::serve_connection(int fd) {
    const auto deadline = std::chrono::steady_clock::now() + kReceiveTime;
    auto finish = [&](int status, const std::string& content_type = "", const std::string& body = "") {
        send_response(fd, status, content_type, body);
        ::shutdown(fd, SHUT_WR);
        // let the peer read the answer before the connection goes away
        char sink[512];
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 200) > 0) (void)::recv(fd, sink, sizeof sink, MSG_DONTWAIT);
        ::close(fd);
    };

    // ---- the request line and the headers ------------------------------------------------------------------
    std::string buf;
    size_t head_end = std::string::npos;
    while (head_end == std::string::npos) {
        if (std::chrono::steady_clock::now() >= deadline) return finish(408);
        const size_t first_eol = buf.find("\r\n");
        if (first_eol == std::string::npos ? buf.size() > kMaxRequestLine : first_eol > kMaxRequestLine) return finish(414);
        if (buf.size() > kMaxRequestLine + kMaxHeaders + 4) return finish(431);
        pollfd p{fd, POLLIN, 0};
        const int pr = ::poll(&p, 1, 200);
        if (pr < 0) return finish(400);
        if (pr == 0) continue;
        char chunk[4096];
        const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
        if (n == 0) {   // the peer closed before the request was complete
            ::close(fd);
            return;
        }
        if (n < 0) {
            ::close(fd);
            return;
        }
        buf.append(chunk, static_cast<size_t>(n));
        head_end = buf.find("\r\n\r\n");
    }
    const std::string head = buf.substr(0, head_end);
    std::string rest = buf.substr(head_end + 4);
    const size_t first_eol = head.find("\r\n");
    const std::string request_line = head.substr(0, first_eol);
    if (request_line.size() > kMaxRequestLine) return finish(414);
    if (first_eol != std::string::npos && head.size() - first_eol > kMaxHeaders + 2) return finish(431);

    HttpRequest req;
    {
        const size_t a = request_line.find(' ');
        const size_t b = a == std::string::npos ? std::string::npos : request_line.find(' ', a + 1);
        if (a == std::string::npos || b == std::string::npos) return finish(400);
        req.method = request_line.substr(0, a);
        const std::string target = request_line.substr(a + 1, b - a - 1);
        const std::string version = request_line.substr(b + 1);
        if (version != "HTTP/1.1" && version != "HTTP/1.0") return finish(400);
        if (target.empty() || target[0] != '/') return finish(400);
        for (unsigned char c : target)
            if (c < 0x21 || c == 0x7f) return finish(400);
        const size_t q = target.find('?');
        req.path = target.substr(0, q);
        if (q != std::string::npos) req.query = target.substr(q + 1);
    }
    if (first_eol != std::string::npos) {
        size_t pos = first_eol + 2;
        while (pos < head.size()) {
            const size_t eol = head.find("\r\n", pos);
            const std::string line = head.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            pos = eol == std::string::npos ? head.size() : eol + 2;
            const size_t colon = line.find(':');
            if (colon == std::string::npos || line[0] == ' ' || line[0] == '\t') return finish(400);
            const std::string name = lower(line.substr(0, colon));
            if (!valid_header_name(name)) return finish(400);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
            if (name == "host" && req.headers.count("host")) return finish(400);
            req.headers[name] = value;
        }
    }

    // ---- the rules, in the order of the contract ---------------------------------------------------------------
    const std::string prefix = "/" + token_;
    const bool token_ok = req.path.size() >= prefix.size() && constant_time_equal(req.path.substr(0, prefix.size()), prefix) &&
                          (req.path.size() == prefix.size() || req.path[prefix.size()] == '/');
    if (!token_ok) return finish(404);
    req.path = req.path.size() > prefix.size() + 1 ? req.path.substr(prefix.size() + 1) : std::string();
    auto host = req.headers.find("host");
    if (host == req.headers.end() || host->second != "127.0.0.1:" + std::to_string(port_)) return finish(400);
    if (req.method != "GET" && req.method != "POST") return finish(405);
    if (req.headers.count("transfer-encoding")) return finish(400);
    for (char c : req.path)
        if (c == '%' || c == '\\' || static_cast<unsigned char>(c) < 0x20) return finish(404);
    if (req.path.find("..") != std::string::npos) return finish(404);

    size_t length = 0;
    auto cl = req.headers.find("content-length");
    if (cl != req.headers.end()) {
        if (cl->second.empty() || cl->second.size() > 12 || !std::all_of(cl->second.begin(), cl->second.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) return finish(400);
        length = static_cast<size_t>(std::stoull(cl->second));
    }
    if (req.method == "POST") {
        auto type = req.headers.find("content-type");
        if (type == req.headers.end() || lower(type->second).compare(0, 16, "application/json") != 0) return finish(415);
        const std::string t = lower(type->second);
        if (t.size() > 16 && t[16] != ';' && t[16] != ' ') return finish(415);
    } else if (length > 0) {
        return finish(400);
    }
    if (length > kMaxBody) return finish(413);

    // ---- the body ------------------------------------------------------------------------------------------------
    std::string body = rest;
    while (body.size() < length) {
        if (std::chrono::steady_clock::now() >= deadline) return finish(408);
        pollfd p{fd, POLLIN, 0};
        const int pr = ::poll(&p, 1, 200);
        if (pr < 0) return finish(400);
        if (pr == 0) continue;
        char chunk[8192];
        const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
        if (n <= 0) {
            ::close(fd);
            return;
        }
        body.append(chunk, static_cast<size_t>(n));
        if (body.size() > length && body.size() - length > 0 && body.size() > kMaxBody + 4096) return finish(413);
    }
    req.body = body.substr(0, length);

    HttpResponse resp = respond(req);
    finish(resp.status, resp.content_type, resp.body);
}

}  // namespace ui
}  // namespace install
}  // namespace pbstore
