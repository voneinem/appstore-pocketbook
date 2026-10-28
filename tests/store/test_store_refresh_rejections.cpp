// T062: a catalog the store must not use (stories 1, FR-002, FR-003): every rejection ends with a message, and the earlier
// catalog, the cache and the remembered serial are unchanged. The serial memory is the larger of state.json and the cache.
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

struct Rig {
    StoreRig rig;
    std::unique_ptr<StoreService> service;
    Rig() {
        if (!rig.ready()) return;
        rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
    }
    StoreOutcome refresh() {
        CancelToken cancel;
        return service->refresh(cancel, nullptr);
    }
    std::string cached_catalog() {
        std::string c, s;
        rig.cache->load(&c, &s);
        return c;
    }
    int64_t serial() { return StateStore(*rig.storage.state).last_serial().value_or(0); }
};

}  // namespace

PB_TEST(a_tampered_catalog_is_e_cat_01_nothing_is_shown_or_stored_and_the_earlier_catalog_stays) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.refresh().ok);
    const std::string cached = r.cached_catalog();
    r.rig.host.publish(101);
    r.rig.host.files["/catalog.json"] += " ";                      // one byte more than what was signed
    StoreOutcome out = r.refresh();
    PB_CHECK(!out.ok);
    PB_CHECK(has_rule(out.findings, "E-CAT-01"));
    PB_CHECK_EQ(r.cached_catalog(), cached);
    PB_CHECK_EQ(r.serial(), int64_t(100));
    PB_CHECK(out.view.catalog.loaded);                             // the earlier catalog is still shown
    PB_CHECK(!out.view.rows.empty());
    PB_CHECK(has(out.messages()[0].text(), "E-CAT-01"));
}

PB_TEST(a_lower_serial_is_e_cat_04_and_a_equal_serial_is_accepted_as_unchanged) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.refresh().ok);
    r.rig.host.publish(90);
    StoreOutcome out = r.refresh();
    PB_CHECK(!out.ok);
    PB_CHECK(has_rule(out.findings, "E-CAT-04"));
    PB_CHECK_EQ(r.serial(), int64_t(100));
    r.rig.host.publish(100);
    StoreOutcome same = r.refresh();
    PB_CHECK(same.ok);
    PB_CHECK_EQ(r.serial(), int64_t(100));
}

PB_TEST(a_catalog_with_a_newer_schema_says_the_store_needs_an_update_through_the_installer_and_the_cache_stays_browsable) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.refresh().ok);
    r.rig.host.catalog.schema_version = "2.0";
    r.rig.host.publish(101);
    StoreOutcome out = r.refresh();
    PB_CHECK(!out.ok);
    PB_CHECK(has_rule(out.findings, "E-CAT-03"));
    const std::string text = out.messages()[0].text();
    PB_CHECK(has(text, "PB Store needs an update"));
    PB_CHECK(has(text, "PB Installer on a computer"));
    PB_CHECK(has(text, "cannot read this catalog"));
    PB_CHECK(out.view.catalog.loaded);                             // the cached older catalog is still browsable
    PB_CHECK(!out.view.rows.empty());
    PB_CHECK_EQ(r.serial(), int64_t(100));
}

PB_TEST(a_cached_catalog_that_fails_its_check_on_load_is_discarded_with_e_sto_09_and_the_list_falls_back_to_the_apps_on_the_reader) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.refresh().ok);
    std::string c, s;
    PB_CHECK(r.rig.cache->load(&c, &s));
    c[c.size() / 2] = static_cast<char>(c[c.size() / 2] ^ 1);
    r.rig.storage.state->write_atomic("catalog.json", c);
    ListView view = r.service->list_cached();
    PB_CHECK(!view.catalog.loaded);
    PB_CHECK(view.catalog.discarded);
    PB_CHECK(has_rule(view.notices, "E-STO-09"));
    PB_CHECK(r.rig.storage.state->size("catalog.json") == UINT64_MAX);      // discarded
    PB_CHECK(r.rig.storage.state->size("state.json") != UINT64_MAX);        // the serial memory stays
    bool found = false;
    for (const auto& row : view.rows) if (row.id == "U_AlphaApp") found = true;   // the apps found on the reader
    PB_CHECK(found);
    PB_CHECK_EQ(r.serial(), int64_t(100));
}

PB_TEST(a_first_run_without_any_serial_is_accepted_with_w_ins_03) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    StoreOutcome out = r.refresh();
    PB_CHECK(out.ok);
    PB_CHECK(has_rule(out.notices, "W-INS-03"));
    PB_CHECK_EQ(r.serial(), int64_t(100));
    StoreOutcome again = r.refresh();
    PB_CHECK(!has_rule(again.notices, "W-INS-03"));
}

PB_TEST(the_serial_memory_is_the_larger_of_state_json_and_the_cache) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.refresh().ok);
    // state.json is deleted: the cache still remembers 100, so an older catalog is refused
    PB_CHECK(r.rig.storage.state->remove("state.json"));
    r.rig.host.publish(90);
    StoreOutcome refused = r.refresh();
    PB_CHECK(!refused.ok);
    PB_CHECK(has_rule(refused.findings, "E-CAT-04"));
    PB_CHECK_EQ(r.serial(), int64_t(100));                         // restored from the cache
    // both are deleted (a first run, or system/pbstore/app deleted): any verified catalog is accepted with the notice
    r.rig.cache->discard();
    PB_CHECK(r.rig.storage.state->remove("state.json"));
    StoreOutcome fresh = r.refresh();
    PB_CHECK(fresh.ok);
    PB_CHECK(has_rule(fresh.notices, "W-INS-03"));
    PB_CHECK_EQ(r.serial(), int64_t(90));
}

PB_TEST_MAIN()
