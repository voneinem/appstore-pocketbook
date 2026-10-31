// T025: the fakes of the store's four device interfaces obey contracts/device-interfaces.md, so that the policy
// tests that use them cannot pass vacuously.
#include "fake_battery.h"
#include "fake_memory.h"
#include "fake_radio.h"
#include "fake_screen.h"
#include "test_support.h"

using namespace pbtest;

PB_TEST(the_fake_radio_starts_in_the_scripted_state_and_records_every_call_in_order) {
    FakeRadio off;
    PB_CHECK(off.is_on() == std::optional<bool>(false));
    PB_CHECK(!off.is_connected());
    FakeRadio on;
    on.set_on(true);
    PB_CHECK(on.is_on() == std::optional<bool>(true));
    PB_CHECK(on.is_connected());                                  // powered and associated
    FakeRadio unreadable;
    unreadable.set_state_unreadable(true);
    PB_CHECK(!unreadable.is_on().has_value());
    PB_CHECK((off.calls() == std::vector<std::string>{"is_on", "is_connected"}));
}

PB_TEST(connect_succeeds_after_the_scripted_number_of_polls) {
    FakeRadio r;
    r.set_connect(FakeRadio::Connect::Succeeds, 2);
    PB_CHECK(r.connect());
    PB_CHECK(r.is_on() == std::optional<bool>(true));             // powered up at once
    PB_CHECK(!r.is_connected());
    PB_CHECK(!r.is_connected());
    PB_CHECK(r.is_connected());                                   // the third poll sees it
    PB_CHECK_EQ(r.last_error(), 0);
}

PB_TEST(a_refused_connect_reports_false_and_an_error_and_a_connect_that_never_completes_times_out) {
    FakeRadio refused;
    refused.set_connect(FakeRadio::Connect::Refused);
    PB_CHECK(!refused.connect());
    PB_CHECK(refused.last_error() != 0);
    PB_CHECK(refused.is_on() == std::optional<bool>(false));      // nothing was switched on
    FakeRadio never;
    never.set_connect(FakeRadio::Connect::NeverConnects);
    PB_CHECK(never.connect());
    for (int i = 0; i < 100; ++i) PB_CHECK(!never.is_connected());
    PB_CHECK(never.is_on() == std::optional<bool>(true));         // powered, never online
}

PB_TEST(disconnect_is_idempotent_and_a_scripted_failure_leaves_the_radio_on) {
    FakeRadio r;
    r.set_on(true);
    PB_CHECK(r.disconnect());
    PB_CHECK(r.is_on() == std::optional<bool>(false));
    PB_CHECK(r.disconnect());                                     // again: still true, still off
    PB_CHECK(r.is_on() == std::optional<bool>(false));
    FakeRadio stuck;
    stuck.set_on(true);
    stuck.set_disconnect_works(false);
    PB_CHECK(!stuck.disconnect());
    PB_CHECK(stuck.is_on() == std::optional<bool>(true));
}

PB_TEST(the_fake_battery_reports_zero_to_one_hundred_or_nothing) {
    FakeBattery b;
    PB_CHECK(b.percent() == std::optional<int>(80));              // the default level of the rig
    b.set_percent(0);
    PB_CHECK(b.percent() == std::optional<int>(0));
    b.set_percent(100);
    PB_CHECK(b.percent() == std::optional<int>(100));
    b.set_percent(101);
    PB_CHECK(!b.percent().has_value());
    b.set_percent(-1);
    PB_CHECK(!b.percent().has_value());
    b.set_percent_unreadable();
    PB_CHECK(!b.percent().has_value());
    PB_CHECK(b.charging() == std::optional<bool>(false));         // the default: not charging
    b.set_charging(true);
    PB_CHECK(b.charging() == std::optional<bool>(true));
    b.set_charging_unreadable();
    PB_CHECK(!b.charging().has_value());
    PB_CHECK_EQ(b.calls().size(), size_t(9));                     // every read is recorded
}

PB_TEST(the_fake_screen_counts_full_updates_and_the_fake_memory_reports_a_scripted_value) {
    FakeScreen s;
    PB_CHECK_EQ(s.full_updates(), 0);
    s.full_update();
    s.full_update();
    PB_CHECK_EQ(s.full_updates(), 2);
    FakeMemory m;
    PB_CHECK(m.available_bytes() == std::optional<uint64_t>(512ull << 20));   // the rig default: 512 MiB
    m.set_available(1234);
    PB_CHECK(m.available_bytes() == std::optional<uint64_t>(1234));
    m.set_unreadable();
    PB_CHECK(!m.available_bytes().has_value());
}

PB_TEST_MAIN()
