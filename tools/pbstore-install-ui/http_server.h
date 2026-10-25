#pragma once
// The assistant's server: a loopback-only HTTP server with the rules of contracts/ui-api.md. It serves what
// the handler returns and knows nothing about installing.

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pbstore {
namespace install {
namespace ui {

struct HttpRequest {
    std::string method;                           // GET or POST
    std::string path;                             // below the token, without the query: "api/state"
    std::string query;                            // after "?", not decoded
    std::map<std::string, std::string> headers;   // lower-case names
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
};

struct HttpServerOptions {
    std::function<std::chrono::steady_clock::time_point()> now;   // injected in tests
    std::function<bool()> busy;                                    // true while an operation runs: the server then stays alive
    std::chrono::seconds idle_after_ping{30};                      // the server ends this long after the last ping
    std::chrono::seconds first_ping_within{300};                   // and this long after the start if no ping ever came
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;
    using Clock = std::chrono::steady_clock;
    using Options = HttpServerOptions;

    explicit HttpServer(Handler handler, Options options = Options());
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Binds 127.0.0.1 on a random port and makes the token (128 random bits, hex).
    bool start(std::string* error);
    int port() const { return port_; }
    const std::string& token() const { return token_; }
    std::string url() const;   // http://127.0.0.1:<port>/<token>/

    // Serves until stop(), POST api/quit, or the idle rule ends it.
    void run();
    void stop() { stop_ = true; }

private:
    void serve_connection(int fd);
    HttpResponse respond(const HttpRequest& req);
    Clock::time_point now() const { return options_.now ? options_.now() : Clock::now(); }

    Handler handler_;
    Options options_;
    int listen_fd_ = -1;
    int port_ = 0;
    std::string token_;
    std::atomic<bool> stop_{false};
    std::atomic<int> active_{0};
    std::mutex mu_;
    Clock::time_point last_ping_;
    Clock::time_point started_;
    bool pinged_ = false;
};

}  // namespace ui
}  // namespace install
}  // namespace pbstore
