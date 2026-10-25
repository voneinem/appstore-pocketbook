// T116: the assistant's server cannot be reached by another page or program on this computer, and cannot be
// used to change anything without the token, the right Host, JSON and the engine's confirmations.
#include "test_support.h"
#include "ui_rig.h"

#include <cstdlib>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {
namespace J = pbstore::json;
J::Value obj() { return J::Value::make_object(); }
}  // namespace

PB_TEST(requests_without_the_token_with_a_foreign_host_or_as_a_simple_cross_origin_post_change_nothing) {
    UiRig u;
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    u.rig.host.add_app(nested_sample_app());
    u.start();
    J::Value open = obj();
    open.set("folder", J::Value::from_string(u.root()));
    PB_CHECK_EQ(u.client().post("api/reader/open", open).status, 200);
    J::Value plan = obj();
    plan.set("action", J::Value::from_string("install"));
    plan.set("app_id", J::Value::from_string("testapp"));
    J::Value planned = u.client().post("api/plan", plan).json();
    J::Value exec = obj();
    exec.set("plan_id", J::Value::from_string(planned.get_string("plan_id").value()));
    J::Value cs = J::Value::make_array();
    for (const auto& c : planned.find("confirmations")->array) cs.push(c);
    exec.set("confirmations", cs);
    const std::string body = J::write(exec);
    const int port = u.server->port();
    const std::string host = "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
    Snapshot before = snapshot(u.root());

    auto post = [&](const std::string& path, const std::string& headers, const std::string& b) {
        return raw_exchange(port, "POST " + path + " HTTP/1.1\r\n" + headers + "Content-Length: " + std::to_string(b.size()) + "\r\n\r\n" + b);
    };
    // no token, a wrong token
    PB_CHECK_EQ(post("/api/execute", host + "Content-Type: application/json\r\n", body).status, 404);
    PB_CHECK_EQ(post("/00000000000000000000000000000000/api/execute", host + "Content-Type: application/json\r\n", body).status, 404);
    // the right token but a foreign Host (DNS rebinding)
    PB_CHECK_EQ(post("/" + u.server->token() + "/api/execute", "Host: attacker.example:" + std::to_string(port) + "\r\nContent-Type: application/json\r\n", body).status, 400);
    // a simple cross-origin POST from a web page: text/plain, which a page can send without a preflight
    PB_CHECK_EQ(post("/" + u.server->token() + "/api/execute", host + "Content-Type: text/plain\r\nOrigin: https://evil.example\r\n", body).status, 415);
    PB_CHECK_EQ(post("/" + u.server->token() + "/api/execute", host + "Content-Type: application/x-www-form-urlencoded\r\n", body).status, 415);
    // a GET to a path that changes things
    PB_CHECK_EQ(raw_exchange(port, "GET /" + u.server->token() + "/api/execute HTTP/1.1\r\n" + host + "\r\n").status, 405);
    PB_CHECK_EQ(raw_exchange(port, "GET /" + u.server->token() + "/api/repair HTTP/1.1\r\n" + host + "\r\n").status, 405);
    // a preflight
    PB_CHECK_EQ(raw_exchange(port, "OPTIONS /" + u.server->token() + "/api/execute HTTP/1.1\r\n" + host + "Origin: https://evil.example\r\nAccess-Control-Request-Method: POST\r\n\r\n").status, 405);
    PB_CHECK(!std::ifstream(u.root() + "/applications/testapp.app").good());
    PB_CHECK(compare(before, snapshot(u.root())).identical);
}

PB_TEST(a_plan_of_another_session_cannot_be_executed_and_the_token_differs_between_starts) {
    UiRig a, b;
    if (!a.ready() || !b.ready()) PB_SKIP("openssl CLI not available");
    for (UiRig* u : {&a, &b}) {
        u->rig.host.add_app(nested_sample_app());
        u->start();
        J::Value open = obj();
        open.set("folder", J::Value::from_string(u->root()));
        PB_CHECK_EQ(u->client().post("api/reader/open", open).status, 200);
    }
    PB_CHECK(a.server->token() != b.server->token());
    J::Value plan = obj();
    plan.set("action", J::Value::from_string("install"));
    plan.set("app_id", J::Value::from_string("testapp"));
    J::Value planned = a.client().post("api/plan", plan).json();
    J::Value exec = obj();
    exec.set("plan_id", J::Value::from_string(planned.get_string("plan_id").value()));
    J::Value cs = J::Value::make_array();
    for (const auto& c : planned.find("confirmations")->array) cs.push(c);
    exec.set("confirmations", cs);
    HttpReply r = b.client().post("api/execute", exec);                          // the plan of session a, sent to session b
    PB_CHECK_EQ(r.status, 422);
    PB_CHECK(!std::ifstream(b.root() + "/applications/testapp.app").good());
    PB_CHECK(!std::ifstream(a.root() + "/applications/testapp.app").good());
    // session a can still use its own plan
    PB_CHECK_EQ(a.client().post("api/execute", exec).status, 200);
    PB_CHECK(!std::ifstream(b.root() + "/applications/testapp.app").good());
}

PB_TEST(the_sources_never_bind_to_all_addresses) {
    const std::string cmd = std::string(PBSTORE_SOURCE_DIR) + "/scripts/check-constitution.sh " + PBSTORE_SOURCE_DIR + " >/dev/null 2>&1";
    PB_CHECK_EQ(std::system(cmd.c_str()), 0);
}

PB_TEST_MAIN()
