// T057a: the reader's wrappers of the four device interfaces (platform/pocketbook/{radio,battery,screen,identity}.cpp), compiled
// against the host shim of inkview.h. Each function is one InkView call plus the conversions named in contracts/device-interfaces.md.
#include "inkview.h"
#include "crash_fs.h"
#include "pbstore/store/identity_parse.h"
#include "platform/pocketbook/inkview_devices.h"
#include "test_support.h"

using namespace pbstore::store;
using pbtest_inkview::state;

namespace {
struct Fresh {
    Fresh() { state().reset(); }
};
}  // namespace

PB_TEST(the_radio_reports_power_from_get_wifi_power_status) {
    Fresh f;
    InkViewRadio r;
    state().wifi_power = 1;
    PB_CHECK(r.is_on() == std::optional<bool>(true));
    state().wifi_power = 0;
    PB_CHECK(r.is_on() == std::optional<bool>(false));
    state().wifi_power = -1;                                       // an error value: unreadable
    PB_CHECK(!r.is_on().has_value());
    PB_CHECK(state().count("GetWiFiPowerStatus") == 3);
}

PB_TEST(the_radio_reports_connected_from_the_net_connected_flags_of_query_network) {
    Fresh f;
    InkViewRadio r;
    // NET_CONNECTED (0x0f00) is the mask of the "ready" bits, NET_WIFIREADY among them: the SDK's own "connected"
    state().query_network = NET_WIFI;                              // the radio exists and is powered, no connection yet
    PB_CHECK(!r.is_connected());
    state().query_network = NET_WIFI | NET_WIFIREADY;              // associated and ready
    PB_CHECK(r.is_connected());
    state().query_network = NET_CONNECTED;
    PB_CHECK(r.is_connected());
    state().query_network = 0;
    PB_CHECK(!r.is_connected());
}

PB_TEST(connect_calls_net_connect_async_once_and_the_callback_status_becomes_the_error) {
    Fresh f;
    InkViewRadio r;
    PB_CHECK(r.connect());
    PB_CHECK_EQ(state().count("NetConnectAsync"), 1);
    PB_CHECK_EQ(state().count("NetConnectSilent"), 0);             // never silent: the system may need to show its own screen
    PB_CHECK_EQ(r.last_error(), 0);                                // nothing has answered yet
    pbtest_inkview::finish_connect(5);                             // the system answers with a failure status
    PB_CHECK_EQ(r.last_error(), 5);
    PB_CHECK(r.connect());
    pbtest_inkview::finish_connect(NET_OK);                        // a success is not an error
    PB_CHECK_EQ(r.last_error(), 0);
}

PB_TEST(a_connect_refused_at_once_reports_false) {
    Fresh f;
    InkViewRadio r;
    state().connect_async_result = -1;
    PB_CHECK(!r.connect());
}

PB_TEST(disconnect_calls_net_disconnect_and_may_be_called_again) {
    Fresh f;
    InkViewRadio r;
    PB_CHECK(r.disconnect());
    PB_CHECK(r.disconnect());                                      // idempotent
    PB_CHECK_EQ(state().count("NetDisconnect"), 2);
    state().disconnect_result = 3;                                 // a status other than NET_OK: it did not work
    PB_CHECK(!r.disconnect());
}

PB_TEST(the_battery_maps_zero_to_one_hundred_and_anything_else_to_nothing) {
    Fresh f;
    InkViewBattery b;
    for (int v : {0, 1, 57, 100}) {
        state().battery = v;
        PB_CHECK(b.percent() == std::optional<int>(v));
    }
    for (int v : {-1, 101, 1000, -999}) {
        state().battery = v;
        PB_CHECK(!b.percent().has_value());
    }
    state().charging = 0;
    PB_CHECK(b.charging() == std::optional<bool>(false));
    state().charging = 1;
    PB_CHECK(b.charging() == std::optional<bool>(true));
}

PB_TEST(the_screen_calls_full_update_exactly_once_per_request) {
    Fresh f;
    InkViewScreen s;
    s.full_update();
    PB_CHECK_EQ(state().count("FullUpdate"), 1);
    s.full_update();
    s.full_update();
    PB_CHECK_EQ(state().count("FullUpdate"), 3);
}

PB_TEST(the_identity_passes_the_two_strings_to_the_parser_and_an_empty_or_null_string_gives_nothing) {
    Fresh f;
    auto src = make_inkview_identity();
    pbtest::CrashFs fs;                                            // the source does not read the volume; it asks InkView
    PB_CHECK(src->model(fs) == std::optional<std::string>("1030"));
    PB_CHECK(src->firmware(fs) == std::optional<std::string>("6.11.1437"));
    state().model = "";
    state().firmware_null = true;
    PB_CHECK(!src->model(fs).has_value());
    PB_CHECK(!src->firmware(fs).has_value());
    state().model_null = true;
    PB_CHECK(!src->model(fs).has_value());
    state().reset();
    PB_CHECK(src->firmware(fs) == std::optional<std::string>("6.11.1437"));
}

PB_TEST_MAIN()
