// T063: the network side of a refresh (FR-001, FR-004 to FR-006, SC-012): Wi-Fi is switched on only for the action and off again,
// every failure has its own plain message, a wrong clock is told apart from other certificate failures, nothing offers a way round
// the certificate check, and the request carries no User-Agent and nothing beyond what an HTTPS request needs.
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Rig {
    StoreRig rig;
    std::unique_ptr<StoreService> service;
    explicit Rig(const Downloader& download = nullptr) {
        if (!rig.ready()) return;
        rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine(download);
        service = rig.make_service();
    }
    StoreOutcome refresh() {
        CancelToken cancel;
        return service->refresh(cancel, nullptr);
    }
    // a second catalog host with another CA: a certificate this reader does not trust
    static std::string foreign_ca(CatalogHost& other) { return other.ca_file(); }
};

}  // namespace

PB_TEST(wifi_off_is_switched_on_for_the_refresh_and_off_again_with_the_notice) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.rig.radio.set_connect(FakeRadio::Connect::Succeeds, 2);
    StoreOutcome out = r.refresh();
    PB_CHECK(out.ok);
    PB_CHECK_EQ(r.rig.radio.count("connect"), 1);
    PB_CHECK_EQ(r.rig.radio.count("disconnect"), 1);
    PB_CHECK(r.rig.radio.is_on() == std::optional<bool>(false));   // SC-012: the state it had before
    PB_CHECK(has_rule(out.notices, "W-STO-03"));
}

PB_TEST(wifi_that_was_already_on_stays_on_and_no_notice_is_shown) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.rig.radio.set_on(true);
    StoreOutcome out = r.refresh();
    PB_CHECK(out.ok);
    PB_CHECK_EQ(r.rig.radio.count("connect"), 0);
    PB_CHECK_EQ(r.rig.radio.count("disconnect"), 0);
    PB_CHECK(r.rig.radio.is_on() == std::optional<bool>(true));
    PB_CHECK(!has_rule(out.notices, "W-STO-03"));
}

PB_TEST(a_refused_connection_or_no_answer_within_30_seconds_is_e_sto_02_and_nothing_changed) {
    for (FakeRadio::Connect mode : {FakeRadio::Connect::Refused, FakeRadio::Connect::NeverConnects}) {
        Rig r;
        if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
        r.rig.radio.set_connect(mode);
        Snapshot before = snapshot(r.rig.root());
        StoreOutcome out = r.refresh();
        PB_CHECK(!out.ok);
        PB_CHECK(has_rule(out.findings, "E-STO-02"));
        PB_CHECK_EQ(r.rig.connection_attempts(), 0);               // never got as far as the network
        PB_CHECK(r.rig.radio.is_on() == std::optional<bool>(false));
        PB_CHECK(r.rig.storage.state->size("catalog.json") == UINT64_MAX);
        Diff d = compare(before, snapshot(r.rig.root()));
        for (const auto& p : d.added) PB_CHECK(p.compare(0, 16, "system/pbstore/a") == 0 || p == "system/pbstore");
        PB_CHECK(d.removed.empty() && d.changed.empty());
        const std::string text = out.messages()[0].text();
        PB_CHECK(has(text, "E-STO-02"));
        PB_CHECK(has(text, "not changed"));
        PB_CHECK(has(text, "Wi-Fi"));
        if (mode == FakeRadio::Connect::NeverConnects) PB_CHECK(r.rig.virtual_ms >= 30000 && r.rig.virtual_ms <= 30500);
    }
}

PB_TEST(a_transfer_without_progress_for_the_stall_time_is_e_ins_36_and_the_reader_is_unchanged) {
    KeyPair key = make_keypair();
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.rig.host.stall_path = "/catalog.json";
    r.rig.host.stall_ms = 4000;
    r.rig.make_engine(r.rig.host.downloader("", 1));               // a stall time of one second keeps the test short
    r.service = r.rig.make_service();
    r.rig.radio.set_on(true);
    StoreOutcome out = r.refresh();
    PB_CHECK(!out.ok);
    PB_CHECK(has_rule(out.findings, "E-INS-36"));
    PB_CHECK(has(out.messages()[0].text(), "Wi-Fi"));
    PB_CHECK(r.rig.storage.state->size("catalog.json") == UINT64_MAX);
}

PB_TEST(a_certificate_failure_with_a_clock_before_the_release_date_is_e_sto_04_with_the_date_the_reader_believes) {
    CatalogHost foreign{make_keypair()};
    if (!foreign.ready()) PB_SKIP("openssl CLI not available");
    Rig r;
    r.rig.make_engine(r.rig.host.downloader("", 30, foreign.ca_file()));     // a CA that did not sign the host's certificate
    r.rig.clock.set_utc(2020, 1, 2, 3, 4, 5);
    r.service = r.rig.make_service();
    r.rig.radio.set_on(true);
    StoreOutcome out = r.refresh();
    PB_CHECK(!out.ok);
    PB_CHECK(has_rule(out.findings, "E-STO-04"));
    const std::string text = out.messages()[0].text();
    PB_CHECK(has(text, "2020-01-02"));                              // the date the reader believes, as YYYY-MM-DD
    PB_CHECK(has(text, "date and time"));
    PB_CHECK(r.rig.storage.state->size("catalog.json") == UINT64_MAX);
}

PB_TEST(a_certificate_failure_with_a_plausible_clock_names_the_certificate_and_the_clock_only_as_one_possible_cause) {
    CatalogHost foreign{make_keypair()};
    if (!foreign.ready()) PB_SKIP("openssl CLI not available");
    Rig r;
    r.rig.make_engine(r.rig.host.downloader("", 30, foreign.ca_file()));
    r.service = r.rig.make_service();                               // the fixed clock is 2026-10-25, after the release date 2026-10-01
    r.rig.radio.set_on(true);
    StoreOutcome out = r.refresh();
    PB_CHECK(!out.ok);
    PB_CHECK(has_rule(out.findings, "E-INS-26"));
    PB_CHECK(!has_rule(out.findings, "E-STO-04"));
    const std::string text = out.messages()[0].text();
    PB_CHECK(has(text, "certificate"));
    PB_CHECK(has(text, "clock"));
}

PB_TEST(no_message_and_no_setting_offers_to_skip_weaken_or_postpone_the_certificate_check) {
    CatalogHost foreign{make_keypair()};
    if (!foreign.ready()) PB_SKIP("openssl CLI not available");
    for (int wrong_clock = 0; wrong_clock < 2; ++wrong_clock) {
        Rig r;
        r.rig.make_engine(r.rig.host.downloader("", 30, foreign.ca_file()));
        if (wrong_clock) r.rig.clock.set_utc(1999, 1, 1, 0, 0, 0);
        r.service = r.rig.make_service();
        r.rig.radio.set_on(true);
        StoreOutcome out = r.refresh();
        PB_CHECK(!out.ok);
        std::string all;
        for (const auto& m : out.messages()) all += lower(m.text()) + "\n";
        for (const char* bad : {"ignore", "skip", "disable", "insecure", "bypass", "postpone", "turn off", "accept the risk"}) PB_CHECK(!has(all, bad));
        SettingsFacts facts = r.service->settings_facts();
        PB_CHECK_EQ(facts.custom_ca, std::string("not supported"));
    }
}

PB_TEST(the_request_carries_no_user_agent_and_no_header_beyond_host_and_what_https_needs) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.rig.radio.set_on(true);
    PB_CHECK(r.refresh().ok);
    PB_CHECK(!r.rig.requests().empty());
    for (const std::string& req : r.rig.requests()) {
        PB_CHECK(lower(req).find("user-agent") == std::string::npos);
        // the header names, in order: the request line is not a header
        std::vector<std::string> names;
        size_t pos = req.find("\r\n");
        while (pos != std::string::npos && pos + 2 < req.size()) {
            const size_t next = req.find("\r\n", pos + 2);
            const std::string line = req.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
            if (line.empty()) break;
            names.push_back(line.substr(0, line.find(':')));
            pos = next;
        }
        PB_CHECK((names == std::vector<std::string>{"Host", "Accept"}));
    }
}

PB_TEST_MAIN()
