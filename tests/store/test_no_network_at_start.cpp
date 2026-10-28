// T064 (SC-004, FR-001, FR-043): the store makes no network connection and touches no Wi-Fi from the start until the user taps
// Refresh, Install, Update or Check, and none after the user left; a refresh requests nothing but the catalog and its signature,
// no icon and no other link of the catalog.
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {
struct Rig {
    StoreRig rig;
    std::unique_ptr<StoreService> service;
    Rig() {
        if (!rig.ready()) return;
        CatalogEntry& e = rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        e.icon_url = "https://github.com/voneinem/appstore-pocketbook/raw/main/icons/testapp.png";
        e.changelog_url = "https://github.com/voneinem/testapp/releases";
        e.source_url = "https://github.com/voneinem/testapp";
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
    }
};
}  // namespace

PB_TEST(constructing_the_service_checking_the_start_listing_the_cache_reading_settings_and_leaving_use_no_network_and_no_radio) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    StartResult start = r.service->start_check();
    (void)start;
    ListView view = r.service->list_cached();
    (void)view;
    SettingsFacts facts = r.service->settings_facts();
    (void)facts;
    r.service->rows("");
    r.service->request_leave(LeaveReason::Home);
    PB_CHECK_EQ(r.rig.connection_attempts(), 0);
    PB_CHECK(r.rig.radio.calls().empty());                         // not even a read of Wi-Fi's state
    PB_CHECK(r.rig.radio.is_on() == std::optional<bool>(false));
}

PB_TEST(a_leave_request_during_an_idle_list_changes_nothing_and_later_calls_still_make_no_connection) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.service->list_cached();
    for (LeaveReason reason : {LeaveReason::Home, LeaveReason::Sleep, LeaveReason::Usb, LeaveReason::ReaderEnded}) r.service->request_leave(reason);
    r.service->list_cached();
    PB_CHECK_EQ(r.rig.connection_attempts(), 0);
    PB_CHECK(r.rig.radio.calls().empty());
}

PB_TEST(a_refresh_requests_only_the_catalog_and_its_signature_and_never_an_icon_or_another_link) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.rig.radio.set_on(true);
    CancelToken cancel;
    PB_CHECK(r.service->refresh(cancel, nullptr).ok);
    const std::vector<std::string> paths = r.rig.host.request_paths();
    PB_CHECK_EQ(paths.size(), size_t(2));
    PB_CHECK_EQ(paths[0], std::string("/catalog.json"));
    PB_CHECK_EQ(paths[1], std::string("/catalog.json.sig"));
    for (const std::string& p : paths) PB_CHECK(p.find("icon") == std::string::npos && p.find("releases") == std::string::npos);
    // browsing afterwards, with the detail data of the entry, still requests nothing
    const int attempts = r.rig.connection_attempts();
    r.service->list_cached();
    r.service->rows("test");
    PB_CHECK_EQ(r.rig.connection_attempts(), attempts);
    // the links are shown as text only
    bool found = false;
    for (const AppRow& row : r.service->rows("")) {
        if (row.id != "testapp") continue;
        found = true;
        PB_CHECK_EQ(row.source_url, std::string("https://github.com/voneinem/testapp"));
        PB_CHECK_EQ(row.changelog_url, std::string("https://github.com/voneinem/testapp/releases"));
    }
    PB_CHECK(found);
}

PB_TEST_MAIN()
