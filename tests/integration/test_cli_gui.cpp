// The start of the assistant from the program: with a graphical session and no arguments (or with `gui`) the
// program starts the server and opens the address; the test build prints the address instead of starting a
// browser. The page and its files are served.
#include "install_rig.h"
#include "test_support.h"
#include "ui_client.h"

#include <csignal>
#include <poll.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace pbtest;

namespace {

struct Started {
    pid_t pid = -1;
    int out_fd = -1;
    std::string url;
};

// Starts the program with the given arguments and environment and reads its output until the address appears.
Started start(const CliRig& r, const std::vector<std::string>& args, const std::vector<std::string>& env, const std::vector<std::string>& unset) {
    Started s;
    int po[2];
    if (::pipe(po)) return s;
    s.pid = ::fork();
    if (s.pid == 0) {
        ::dup2(po[1], 1);
        ::close(po[0]);
        ::close(po[1]);
        for (const std::string& n : unset) ::unsetenv(n.c_str());
        for (const std::string& e : env) {
            const size_t eq = e.find('=');
            ::setenv(e.substr(0, eq).c_str(), e.substr(eq + 1).c_str(), 1);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(PBSTORE_INSTALL_TEST_TOOL));
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        ::execv(PBSTORE_INSTALL_TEST_TOOL, argv.data());
        ::_exit(127);
    }
    ::close(po[1]);
    s.out_fd = po[0];
    std::string seen;
    for (int i = 0; i < 100 && s.url.empty(); ++i) {
        pollfd p{s.out_fd, POLLIN, 0};
        if (::poll(&p, 1, 100) > 0) {
            char buf[1024];
            const ssize_t n = ::read(s.out_fd, buf, sizeof buf);
            if (n <= 0) break;
            seen.append(buf, static_cast<size_t>(n));
            const size_t at = seen.find("OPENED ");
            if (at != std::string::npos && seen.find('\n', at) != std::string::npos) {
                const size_t end = seen.find('\n', at);
                s.url = seen.substr(at + 7, end - at - 7);
            }
        }
    }
    (void)r;
    return s;
}

int wait_exit(Started& s, int seconds) {
    for (int i = 0; i < seconds * 10; ++i) {
        int status = 0;
        if (::waitpid(s.pid, &status, WNOHANG) == s.pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ::kill(s.pid, SIGKILL);
    ::waitpid(s.pid, nullptr, 0);
    return -2;
}

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

void check_assistant(const std::string& url) {
    // http://127.0.0.1:PORT/TOKEN/
    PB_CHECK(url.compare(0, 17, "http://127.0.0.1:") == 0);
    const size_t colon = url.find(':', 7);
    const size_t slash = url.find('/', colon);
    const int port = std::atoi(url.c_str() + colon + 1);
    const std::string token = url.substr(slash + 1, url.size() - slash - 2);
    UiClient c(port, token);
    HttpReply page = c.get("");
    PB_CHECK_EQ(page.status, 200);
    PB_CHECK(has(page.headers["content-type"], "text/html"));
    PB_CHECK(has(page.body, "PB Installer"));
    PB_CHECK(has(c.get("app.js").headers["content-type"], "javascript"));
    PB_CHECK(has(c.get("style.css").headers["content-type"], "text/css"));
    PB_CHECK_EQ(c.get("api/state").status, 200);
    PB_CHECK_EQ(c.post("api/quit", "{}").status, 200);
}

}  // namespace

PB_TEST(without_arguments_and_with_a_graphical_session_the_assistant_starts_and_ends_on_quit) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    Started s = start(r, {}, r.env({"DISPLAY=:99", "PBSTORE_TEST_OPEN_URL=print", "PBSTORE_TEST_MOUNTS="}), {"WAYLAND_DISPLAY"});
    PB_CHECK(!s.url.empty());
    check_assistant(s.url);
    PB_CHECK_EQ(wait_exit(s, 10), 0);
}

PB_TEST(a_wayland_session_counts_and_the_gui_command_starts_it_too) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    Started a = start(r, {}, r.env({"WAYLAND_DISPLAY=wayland-1", "PBSTORE_TEST_OPEN_URL=print", "PBSTORE_TEST_MOUNTS="}), {"DISPLAY"});
    PB_CHECK(!a.url.empty());
    check_assistant(a.url);
    PB_CHECK_EQ(wait_exit(a, 10), 0);
    Started b = start(r, {"gui"}, r.env({"PBSTORE_TEST_OPEN_URL=print", "PBSTORE_TEST_MOUNTS="}), {"DISPLAY", "WAYLAND_DISPLAY"});
    PB_CHECK(!b.url.empty());
    check_assistant(b.url);
    PB_CHECK_EQ(wait_exit(b, 10), 0);
}

PB_TEST(an_interrupt_from_the_keyboard_ends_the_assistant) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    Started s = start(r, {"gui"}, r.env({"PBSTORE_TEST_OPEN_URL=print", "PBSTORE_TEST_MOUNTS="}), {"DISPLAY", "WAYLAND_DISPLAY"});
    PB_CHECK(!s.url.empty());
    ::kill(s.pid, SIGINT);
    PB_CHECK_EQ(wait_exit(s, 10), 0);
}

PB_TEST_MAIN()
