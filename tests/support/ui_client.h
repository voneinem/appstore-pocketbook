#pragma once
// Test helper: a tiny HTTP client over raw sockets for the assistant's server (no browser, no display).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <map>
#include <string>

#include "pbstore/json.h"

namespace pbtest {

struct HttpReply {
    int status = 0;
    std::string head, body;
    std::map<std::string, std::string> headers;   // lower-case names
    bool connected = false;
    pbstore::json::Value json() const {
        auto v = pbstore::json::parse(body);
        return v ? *v : pbstore::json::Value();
    }
    bool has_header(const std::string& n) const { return headers.count(n) > 0; }
};

// Sends `bytes`, then reads the answer until the server closes or `wait_ms` pass without data.
inline HttpReply raw_exchange(int port, const std::string& bytes, int wait_ms = 3000, bool shutdown_write = false) {
    HttpReply r;
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(fd);
        return r;
    }
    r.connected = true;
    size_t sent = 0;
    while (sent < bytes.size()) {
        ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
    if (shutdown_write) ::shutdown(fd, SHUT_WR);
    std::string all;
    while (true) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, wait_ms) <= 0) break;
        char buf[8192];
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        all.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);
    const size_t split = all.find("\r\n\r\n");
    r.head = split == std::string::npos ? all : all.substr(0, split);
    r.body = split == std::string::npos ? "" : all.substr(split + 4);
    if (r.head.compare(0, 9, "HTTP/1.1 ") == 0) r.status = std::atoi(r.head.c_str() + 9);
    size_t line_start = r.head.find("\r\n");
    while (line_start != std::string::npos) {
        const size_t begin = line_start + 2;
        const size_t end = r.head.find("\r\n", begin);
        const std::string line = r.head.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon), value = line.substr(colon + 1);
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            r.headers[name] = value;
        }
        line_start = end;
    }
    return r;
}

class UiClient {
public:
    UiClient(int port, std::string token) : port_(port), token_(std::move(token)) {}
    HttpReply get(const std::string& path, int wait_ms = 20000) const {
        return raw_exchange(port_, "GET /" + token_ + "/" + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port_) + "\r\nConnection: close\r\n\r\n", wait_ms);
    }
    HttpReply post(const std::string& path, const std::string& json_body = "{}", int wait_ms = 60000) const {
        return raw_exchange(port_, "POST /" + token_ + "/" + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port_) +
                                       "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(json_body.size()) + "\r\nConnection: close\r\n\r\n" + json_body,
                            wait_ms);
    }
    HttpReply post(const std::string& path, const pbstore::json::Value& body) const { return post(path, pbstore::json::write(body)); }
    int port() const { return port_; }
    const std::string& token() const { return token_; }

private:
    int port_;
    std::string token_;
};

}  // namespace pbtest
