// T061: the list of the store (stories 1 and 5, FR-008, FR-012, FR-013): every offered app has exactly one state, apps found on the
// reader that are not in the catalog are labelled, withdrawn versions are not offered, the list is computed from the reader's
// current contents, and a restart with no network lists the same apps from the cache without any network or radio call.
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

const AppRow* find(const std::vector<AppRow>& rows, const std::string& id) {
    for (const auto& r : rows) if (r.id == id) return &r;
    return nullptr;
}
bool has_action(const AppRow& r, RowAction a) { return std::find(r.actions.begin(), r.actions.end(), a) != r.actions.end(); }
bool has_note(const AppRow& r, const std::string& n) { return std::find(r.notes.begin(), r.notes.end(), n) != r.notes.end(); }

// testapp 1.2.0 is installed and 1.3.0 is offered; "other" is offered and not installed; the reader also holds three hand-installed
// apps (Alpha, Beta, Gamma) that the catalog does not know, and the vendor's own entry
FakeReader reader_with_testapp() { return with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")); }

void publish_catalog(StoreRig& rig) {
    CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    e.listed_versions = {"1.2.0", "1.3.0"};
    e.short_description.en = "A test app";
    rig.host.add_app(nested_sample_app("other", "1.0.0", "U_Other"));
    rig.host.publish(100);
}

}  // namespace

PB_TEST(refresh_lists_every_offered_app_with_exactly_one_of_the_four_states_and_labels_the_unknown_ones) {
    StoreRig rig(reader_with_testapp());
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    publish_catalog(rig);
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    CancelToken cancel;
    StoreOutcome out = service->refresh(cancel, nullptr);
    PB_CHECK(out.ok);
    PB_CHECK(out.view.catalog.loaded);
    const AppRow* testapp = find(out.view.rows, "testapp");
    const AppRow* other = find(out.view.rows, "other");
    PB_CHECK(testapp != nullptr && other != nullptr);
    PB_CHECK(testapp->state == RowState::UpdateAvailable);
    PB_CHECK_EQ(testapp->installed_version, std::string("1.2.0"));
    PB_CHECK_EQ(testapp->offered_version, std::string("1.3.0"));
    PB_CHECK_EQ(testapp->short_description, std::string("A test app"));
    PB_CHECK(other->state == RowState::NotInstalled);
    PB_CHECK_EQ(other->version, std::string("1.0.0"));
    // the hand-installed apps are listed with the separate label; the vendor's own entry never is
    for (const char* id : {"U_AlphaApp", "U_BetaApp", "U_GammaApp"}) {
        const AppRow* r = find(out.view.rows, id);
        PB_CHECK(r != nullptr);
        if (r) PB_CHECK(r->state == RowState::NotInCatalog);
    }
    PB_CHECK(find(out.view.rows, "Dictionary") == nullptr);
    // the offered apps of the catalog come first (by name), then the apps that are not in the catalog (by name)
    bool seen_unknown = false;
    for (const auto& r : out.view.rows) {
        if (r.state == RowState::NotInCatalog) seen_unknown = true;
        else PB_CHECK(!seen_unknown);
    }
}

PB_TEST(actions_follow_the_state_and_the_store_itself_has_no_remove) {
    StoreRig rig(reader_with_testapp());
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    publish_catalog(rig);
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    CancelToken cancel;
    StoreOutcome out = service->refresh(cancel, nullptr);
    const AppRow* testapp = find(out.view.rows, "testapp");
    const AppRow* other = find(out.view.rows, "other");
    const AppRow* alpha = find(out.view.rows, "U_AlphaApp");
    PB_CHECK(has_action(*other, RowAction::Install) && !has_action(*other, RowAction::Remove));
    PB_CHECK(has_action(*testapp, RowAction::Update) && has_action(*testapp, RowAction::Remove) && !has_action(*testapp, RowAction::Install));
    PB_CHECK(alpha->actions.empty());                              // no install record: nothing to remove, nothing to update
}

PB_TEST(a_withdrawn_version_is_not_offered_and_an_installed_withdrawn_copy_is_noted_but_never_changed) {
    StoreRig rig(reader_with_testapp());
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    e.listed_versions = {"1.2.0", "1.3.0"};
    e.withdrawn_versions = {"1.2.0"};                              // the installed one is withdrawn; the newer one is fine
    CatalogEntry& gone = rig.host.add_app(nested_sample_app("other", "1.0.0", "U_Other"));
    gone.withdrawn_versions = {"1.0.0"};                           // the only version is withdrawn: not offered
    rig.host.publish(100);
    rig.write_reader();
    Snapshot before = snapshot(rig.root());
    rig.make_engine();
    auto service = rig.make_service();
    CancelToken cancel;
    StoreOutcome out = service->refresh(cancel, nullptr);
    PB_CHECK(out.ok);
    const AppRow* testapp = find(out.view.rows, "testapp");
    PB_CHECK(testapp != nullptr);
    PB_CHECK(has_note(*testapp, "this version was withdrawn"));
    PB_CHECK(testapp->state == RowState::UpdateAvailable);         // Update is still offered when a newer version is offered
    PB_CHECK(has_action(*testapp, RowAction::Update));
    const AppRow* other = find(out.view.rows, "other");
    PB_CHECK(other != nullptr);
    PB_CHECK(!has_action(*other, RowAction::Install));             // withdrawn: not offered for install
    // listing never changes the reader apart from the store's own state folder
    Snapshot after = snapshot(rig.root());
    Diff d = compare(before, after);
    for (const auto& p : d.added) PB_CHECK(p.compare(0, 18, "system/pbstore/app") == 0 || p == "system/pbstore" || p == "system/pbstore/app");
    PB_CHECK(d.removed.empty() && d.changed.empty());
}

PB_TEST(states_are_computed_from_the_readers_current_contents) {
    StoreRig rig(reader_with_testapp());
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    publish_catalog(rig);
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    CancelToken cancel;
    PB_CHECK(service->refresh(cancel, nullptr).ok);
    PB_CHECK(find(service->rows(""), "testapp")->state == RowState::UpdateAvailable);
    // someone deletes the install record on the reader: the entry is still in the menu, so the app is "installed outside the store"
    PB_CHECK(::unlink((rig.root() + "/system/pbstore/installed/testapp.json").c_str()) == 0);
    ListView view = service->list_cached();
    PB_CHECK(find(view.rows, "testapp")->state == RowState::InstalledOutside);
    PB_CHECK(has_action(*find(view.rows, "testapp"), RowAction::Check));
}

PB_TEST(after_a_restart_with_wifi_off_the_same_apps_come_from_the_cache_without_any_network_or_radio_call) {
    StoreRig rig(reader_with_testapp());
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    publish_catalog(rig);
    rig.write_reader();
    rig.make_engine();
    std::vector<std::string> first_ids;
    {
        auto service = rig.make_service();
        CancelToken cancel;
        StoreOutcome out = service->refresh(cancel, nullptr);
        PB_CHECK(out.ok);
        for (const auto& r : out.view.rows) first_ids.push_back(r.id);
    }
    // a restart: a fresh engine whose downloader must never be called, a fresh service, the radio off and never touched
    int calls = 0;
    Downloader never = [&calls](const std::string&, uint64_t, long, const DownloadProgress&) {
        ++calls;
        FetchResult r;
        r.ok = false;
        r.kind = FetchKind::Unreachable;
        return r;
    };
    rig.make_engine(never);
    rig.radio = FakeRadio();
    const int attempts_before = rig.connection_attempts();
    auto service = rig.make_service();
    ListView view = service->list_cached();
    std::vector<std::string> ids;
    for (const auto& r : view.rows) ids.push_back(r.id);
    PB_CHECK((ids == first_ids));
    PB_CHECK(view.catalog.loaded);
    PB_CHECK_EQ(calls, 0);
    PB_CHECK(rig.radio.calls().empty());
    PB_CHECK_EQ(rig.connection_attempts(), attempts_before);
    PB_CHECK(find(view.rows, "testapp")->state == RowState::UpdateAvailable);
}

PB_TEST(last_updated_is_the_refresh_date_and_the_generation_date_of_the_catalog_is_separate) {
    StoreRig rig(reader_with_testapp());
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    publish_catalog(rig);
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    CancelToken cancel;
    StoreOutcome out = service->refresh(cancel, nullptr);
    PB_CHECK_EQ(out.view.catalog.refreshed_at, std::string("2026-10-25T12:00:00Z"));     // the reader's clock at the refresh
    PB_CHECK_EQ(out.view.catalog.generated_at, std::string("2026-10-24T12:00:00Z"));     // the catalog's own date
    SettingsFacts facts = service->settings_facts();
    PB_CHECK_EQ(facts.refreshed_at, out.view.catalog.refreshed_at);
    PB_CHECK_EQ(facts.generated_at, out.view.catalog.generated_at);
    PB_CHECK_EQ(facts.catalog_url, rig.host.catalog_url());
    PB_CHECK_EQ(facts.custom_ca, std::string("not supported"));
    PB_CHECK(facts.log_location.find("system/pbstore/app/pbstore.log") != std::string::npos);
}

PB_TEST(with_no_cache_and_no_connection_the_list_shows_what_is_on_the_reader_and_says_that_no_catalog_was_downloaded) {
    StoreRig rig(reader_with_testapp());
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    ListView view = service->list_cached();
    PB_CHECK(!view.catalog.loaded);
    PB_CHECK(view.catalog.refreshed_at.empty());
    PB_CHECK(!view.catalog.discarded);                             // never downloaded, which is not the same as discarded
    const AppRow* testapp = find(view.rows, "testapp");
    PB_CHECK(testapp != nullptr);
    PB_CHECK(testapp->state == RowState::NotInCatalog);            // installed with a record, but no catalog to compare with
    PB_CHECK(has_action(*testapp, RowAction::Remove));             // removal works with no network (story 4)
    PB_CHECK(find(view.rows, "U_AlphaApp") != nullptr);
    PB_CHECK(find(view.rows, "Dictionary") == nullptr);
    PB_CHECK_EQ(rig.connection_attempts(), 0);
}

PB_TEST_MAIN()
