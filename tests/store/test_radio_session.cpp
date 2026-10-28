// T036: Wi-Fi is switched on only for a user action and is back in its earlier state afterwards, however the action
// ends (spec FR-001, SC-012; research R-06). The clock is injected: the test never waits.
#include "fake_radio.h"
#include "pbstore/store/radio_session.h"
#include "test_support.h"

using namespace pbstore::store;
using pbtest::FakeRadio;

namespace {

// A virtual clock: sleeping moves it.
struct Time {
    int64_t ms = 0;
    int sleeps = 0;
    std::vector<int> naps;
    RadioSession::Sleep sleep() {
        return [this](int n) { ms += n; ++sleeps; naps.push_back(n); };
    }
    RadioSession::Now now() {
        return [this] { return ms; };
    }
};

bool has_rule(const std::optional<pbstore::Finding>& f, const char* id) { return f && f->rule_id == id; }

}  // namespace

PB_TEST(the_constants_are_30_seconds_and_500_milliseconds) {
    PB_CHECK_EQ(kConnectSeconds, 30);
    PB_CHECK_EQ(kPollMs, 500);
}

PB_TEST(a_radio_that_is_already_on_is_left_alone) {
    FakeRadio radio;
    radio.set_on(true);
    Time t;
    RadioSession s(radio, t.sleep(), t.now());
    PB_CHECK(!s.begin().has_value());                               // no finding: ready
    PB_CHECK_EQ(radio.count("connect"), 0);
    PB_CHECK(!s.switched_on_by_store());
    PB_CHECK(!s.end().has_value());                                 // no notice, nothing switched off
    PB_CHECK_EQ(radio.count("disconnect"), 0);
    PB_CHECK(radio.powered());
}

PB_TEST(a_radio_that_is_off_is_switched_on_waited_for_and_switched_off_again_with_the_notice) {
    FakeRadio radio;
    radio.set_connect(FakeRadio::Connect::Succeeds, 3);
    Time t;
    RadioSession s(radio, t.sleep(), t.now());
    PB_CHECK(!s.begin().has_value());
    PB_CHECK_EQ(radio.count("connect"), 1);
    PB_CHECK(s.switched_on_by_store());
    PB_CHECK_EQ(t.sleeps, 3);                                       // polled every 500 ms until connected
    for (int n : t.naps) PB_CHECK_EQ(n, 500);
    auto notice = s.end();
    PB_CHECK(has_rule(notice, "W-STO-03"));
    PB_CHECK(!radio.powered());                                     // back in its earlier state (SC-012)
    PB_CHECK_EQ(radio.count("disconnect"), 1);
    PB_CHECK(!s.end().has_value());                                 // end() is idempotent: no second notice, no second call
    PB_CHECK_EQ(radio.count("disconnect"), 1);
}

PB_TEST(no_connection_within_30_seconds_is_e_sto_02_and_the_radio_is_switched_off_again) {
    FakeRadio radio;
    radio.set_connect(FakeRadio::Connect::NeverConnects);
    Time t;
    RadioSession s(radio, t.sleep(), t.now());
    auto f = s.begin();
    PB_CHECK(has_rule(f, "E-STO-02"));
    PB_CHECK(t.ms >= 30000 && t.ms <= 30500);
    auto notice = s.end();
    PB_CHECK(has_rule(notice, "W-STO-03"));
    PB_CHECK(!radio.powered());
}

PB_TEST(a_refused_connect_is_e_sto_02_at_once_and_nothing_was_switched_on) {
    FakeRadio radio;
    radio.set_connect(FakeRadio::Connect::Refused);
    Time t;
    RadioSession s(radio, t.sleep(), t.now());
    PB_CHECK(has_rule(s.begin(), "E-STO-02"));
    PB_CHECK_EQ(t.sleeps, 0);
    PB_CHECK(!s.switched_on_by_store());
    PB_CHECK(!s.end().has_value());
    PB_CHECK_EQ(radio.count("disconnect"), 0);
}

PB_TEST(cancel_while_waiting_stops_the_wait_and_the_radio_is_switched_off_again) {
    FakeRadio radio;
    radio.set_connect(FakeRadio::Connect::NeverConnects);
    Time t;
    RadioSession s(radio, t.sleep(), t.now());
    int polls = 0;
    auto f = s.begin([&polls] { return ++polls > 4; });
    PB_CHECK(has_rule(f, "E-STO-02"));
    PB_CHECK(t.ms < 5000);
    PB_CHECK(has_rule(s.end(), "W-STO-03"));
    PB_CHECK(!radio.powered());
}

PB_TEST(an_unreadable_earlier_state_counts_as_on_and_is_never_switched_off) {
    FakeRadio radio;
    radio.set_state_unreadable(true);
    Time t;
    RadioSession s(radio, t.sleep(), t.now());
    PB_CHECK(!s.begin().has_value());
    PB_CHECK_EQ(radio.count("connect"), 0);
    PB_CHECK(!s.end().has_value());
    PB_CHECK_EQ(radio.count("disconnect"), 0);
}

PB_TEST(a_disconnect_that_fails_or_leaves_the_radio_on_is_w_sto_02) {
    FakeRadio radio;
    radio.set_disconnect_works(false);
    Time t;
    RadioSession s(radio, t.sleep(), t.now());
    PB_CHECK(!s.begin().has_value());
    PB_CHECK(has_rule(s.end(), "W-STO-02"));
    PB_CHECK(radio.powered());
}

PB_TEST(end_is_legal_before_begin_and_after_every_outcome) {
    FakeRadio radio;
    Time t;
    {
        RadioSession never_begun(radio, t.sleep(), t.now());
        PB_CHECK(!never_begun.end().has_value());
        PB_CHECK_EQ(radio.count("disconnect"), 0);
    }
    PB_CHECK(radio.calls().empty() || radio.count("connect") == 0);
}

PB_TEST(an_exception_thrown_by_the_caller_still_restores_the_radio) {
    FakeRadio radio;
    Time t;
    try {
        RadioSession s(radio, t.sleep(), t.now());
        PB_CHECK(!s.begin().has_value());
        PB_CHECK(radio.powered());
        throw std::runtime_error("the transfer blew up");
    } catch (const std::runtime_error&) {
    }
    PB_CHECK(!radio.powered());                                     // the destructor ended the session
}

PB_TEST(a_leave_request_ends_the_session_the_same_way) {
    FakeRadio radio;
    Time t;
    RadioSession s(radio, t.sleep(), t.now());
    PB_CHECK(!s.begin().has_value());
    auto notice = s.end();                                          // what request_leave() calls
    PB_CHECK(has_rule(notice, "W-STO-03"));
    PB_CHECK(!radio.powered());
}

PB_TEST_MAIN()
