// T061: the state of each app is derived, never stored (data-model.md, App State).
#include "install_fixtures.h"
#include "pbstore/install/app_state.h"
#include "sample_catalog.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {
Identity known() { return Identity{std::string("1030"), std::string("6.11.1437")}; }

Catalog catalog_with(const std::string& id, const std::string& version, const std::string& menu = "U_TestApp") {
    Catalog c = sample_catalog(100);
    c.apps.clear();
    CatalogEntry e = sample_entry(id, version);
    e.menu_entry_id = menu;
    c.apps.push_back(e);
    return c;
}
// find() on a temporary vector keeps the vector alive, so `find(derive_app_states(...), id)->x` does not point into a temporary
struct Found {
    std::vector<AppEntry> all;
    const AppEntry* entry = nullptr;
    Found() = default;
    Found(const AppEntry* e) : entry(e) {}
    const AppEntry* operator->() const { return entry; }
    operator const AppEntry*() const { return entry; }
};
const AppEntry* find(const std::vector<AppEntry>& v, const std::string& id) {
    for (const auto& a : v) if (a.id == id) return &a;
    return nullptr;
}
Found find(std::vector<AppEntry>&& v, const std::string& id) {
    Found f;
    f.all = std::move(v);
    f.entry = find(f.all, id);
    return f;
}
}  // namespace

PB_TEST(an_app_that_is_only_in_the_catalog_is_not_installed) {
    FakeReader r;
    Volume v(r);
    Catalog c = catalog_with("testapp", "1.2.0");
    auto apps = derive_app_states(&c, scan(v.fat, known(), ScanRequest{}));
    Found a = find(apps, "testapp");
    PB_CHECK(a != nullptr);
    PB_CHECK(a->state == AppStateKind::NotInstalled);
    PB_CHECK(a->offered);
    PB_CHECK_EQ(a->offered_version, std::string("1.2.0"));
    PB_CHECK(!a->update_available);
}

PB_TEST(an_installed_app_is_marked_for_update_only_when_a_newer_offered_version_exists) {
    FakeReader base;
    FakeReader r = with_installed(base, nested_sample_app("testapp", "1.2.0"));
    Volume v(r);
    ReaderSnapshot s = scan(v.fat, known(), ScanRequest{});
    {
        Catalog c = catalog_with("testapp", "1.3.0");
        Found a = find(derive_app_states(&c, s), "testapp");
        PB_CHECK(a->state == AppStateKind::Installed);
        PB_CHECK(a->update_available);
        PB_CHECK_EQ(a->installed_version, std::string("1.2.0"));
        PB_CHECK_EQ(a->offered_version, std::string("1.3.0"));
    }
    {   // semantic versions, not text: 1.10.0 is newer than 1.9.0
        FakeReader base2;
        FakeReader r2 = with_installed(base2, nested_sample_app("testapp", "1.9.0"));
        Volume v2(r2);
        Catalog c = catalog_with("testapp", "1.10.0");
        PB_CHECK(find(derive_app_states(&c, scan(v2.fat, known(), ScanRequest{})), "testapp")->update_available);
    }
    {   // the same version, or an older one, is not an update
        Catalog same = catalog_with("testapp", "1.2.0");
        PB_CHECK(!find(derive_app_states(&same, s), "testapp")->update_available);
        Catalog older = catalog_with("testapp", "1.1.0");
        PB_CHECK(!find(derive_app_states(&older, s), "testapp")->update_available);
    }
}

PB_TEST(a_withdrawn_offered_version_is_never_offered_and_an_installed_copy_is_not_touched) {
    FakeReader base;
    FakeReader r = with_installed(base, nested_sample_app("testapp", "1.2.0"));
    Volume v(r);
    ReaderSnapshot s = scan(v.fat, known(), ScanRequest{});
    Catalog c = catalog_with("testapp", "1.3.0");
    c.apps[0].withdrawn_versions = {"1.3.0"};
    c.apps[0].listed_versions = {"1.2.0", "1.3.0"};
    Found a = find(derive_app_states(&c, s), "testapp");
    PB_CHECK(!a->offered);
    PB_CHECK(!a->update_available);
    PB_CHECK(a->state == AppStateKind::Installed);
    // the installed version itself withdrawn: shown as information, nothing is removed
    Catalog w = catalog_with("testapp", "1.3.0");
    w.apps[0].withdrawn_versions = {"1.2.0"};
    w.apps[0].listed_versions = {"1.2.0", "1.3.0"};
    Found b = find(derive_app_states(&w, s), "testapp");
    PB_CHECK(b->installed_version_withdrawn);
    PB_CHECK(b->update_available);
    PB_CHECK(b->state == AppStateKind::Installed);
}

PB_TEST(changed_local_and_not_in_catalog_are_flags) {
    FakeReader base;
    FakeReader r = with_installed(base, nested_sample_app("testapp", "1.2.0"));
    r.files["applications/testapp.bin"] = "edited";
    Volume v(r);
    ScanRequest all;
    all.all_recorded = true;
    ReaderSnapshot s = scan(v.fat, known(), all);
    Catalog c = catalog_with("testapp", "1.2.0");
    Found a = find(derive_app_states(&c, s), "testapp");
    PB_CHECK(a->changed);
    PB_CHECK_EQ(a->changed_files.size(), size_t(1));
    PB_CHECK(!a->not_in_catalog);
    Catalog none = catalog_with("somethingelse", "1.0.0", "U_SomethingElse");
    auto apps = derive_app_states(&none, s);
    PB_CHECK(find(apps, "testapp")->not_in_catalog);
    // a local install
    FakeReader l = with_installed(FakeReader(), nested_sample_app("localapp", "0.5.0", "U_LocalApp"));
    std::string rec = l.files["system/pbstore/installed/localapp.json"];
    rec.replace(rec.find("\"catalog\""), 9, "\"local\"");
    l.files["system/pbstore/installed/localapp.json"] = rec;
    Volume lv(l);
    auto lapps = derive_app_states(&none, scan(lv.fat, known(), ScanRequest{}));
    PB_CHECK(find(lapps, "localapp")->from_local);
    PB_CHECK_EQ(find(lapps, "localapp")->source, std::string("local"));
}

PB_TEST(an_entry_without_a_record_is_installed_outside_the_installer_or_unknown) {
    FakeReader r;
    r.add_hand_installed(make_sample_app("testapp", "1.2.0", "U_TestApp", "system/testapp"));   // files and entry, no record
    Volume v(r);
    Catalog c = catalog_with("testapp", "1.2.0");
    auto apps = derive_app_states(&c, scan(v.fat, known(), ScanRequest{}));
    PB_CHECK(find(apps, "testapp")->state == AppStateKind::OutsideInstaller);
    // the other U_ entries of the fake reader are neither recorded nor in the catalog
    const AppEntry* alpha = find(apps, "U_AlphaApp");
    PB_CHECK(alpha != nullptr);
    PB_CHECK(alpha->state == AppStateKind::UnknownApp);
    PB_CHECK_EQ(alpha->name, std::string("Alpha"));
    // entries without the U_ prefix belong to the vendor: never listed
    PB_CHECK(find(apps, "Dictionary") == nullptr);
}

PB_TEST(a_record_without_its_menu_entry_is_shown_as_such) {
    FakeReader base;
    FakeReader r = with_installed(base, nested_sample_app());
    // take the entry out of the desktop configuration
    ParseResult p = parse_desktop_config(r.files["system/config/desktop/view.json"]);
    r.files["system/config/desktop/view.json"] = *edit(*p.config, remove_entry("U_TestApp")).text;
    Volume v(r);
    Catalog c = catalog_with("testapp", "1.2.0");
    Found a = find(derive_app_states(&c, scan(v.fat, known(), ScanRequest{})), "testapp");
    PB_CHECK(a->state == AppStateKind::Installed);
    PB_CHECK(a->menu_entry_missing);
}

PB_TEST(the_list_is_ordered_by_name_and_works_without_a_catalog) {
    FakeReader r;
    Volume v(r);
    Catalog c = sample_catalog(100);   // alpha-app, beta-app
    auto apps = derive_app_states(&c, scan(v.fat, known(), ScanRequest{}));
    std::vector<std::string> names;
    for (const auto& a : apps) names.push_back(a.name);
    PB_CHECK(std::is_sorted(names.begin(), names.end()));
    auto offline = derive_app_states(nullptr, scan(v.fat, known(), ScanRequest{}));
    PB_CHECK(find(offline, "alpha-app") == nullptr);
    PB_CHECK(find(offline, "U_AlphaApp") != nullptr);
}

PB_TEST_MAIN()
