// T087: a withdrawn version is never offered for install or update; an installed copy is shown (information)
// but never removed, replaced or changed because of a withdrawal.
#include "install_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {
bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
const AppEntry* find(const std::vector<AppEntry>& v, const std::string& id) {
    for (const auto& a : v) if (a.id == id) return &a;
    return nullptr;
}
}  // namespace

PB_TEST(a_withdrawn_newest_version_is_not_offered_for_install_or_update) {
    for (PlanKind kind : {PlanKind::Install, PlanKind::Update}) {
        FakeReader base;
        FakeReader reader = kind == PlanKind::Update ? with_installed(base, nested_sample_app("testapp", "1.2.0")) : base;
        EngineRig rig(reader);
        if (!rig.ready()) PB_SKIP("openssl CLI not available");
        CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
        e.withdrawn_versions = {"1.3.0"};
        e.listed_versions = {"1.2.0", "1.3.0"};
        rig.host.publish(100);
        rig.write_reader();
        Snapshot before = snapshot(rig.root());
        Engine& engine = rig.make_engine();
        auto reader_handle = engine.open_reader(rig.root());
        PB_CHECK(reader_handle.ok());
        CancelToken cancel;
        Action action;
        action.type = kind == PlanKind::Update ? Action::Type::Update : Action::Type::Install;
        action.app_id = "testapp";
        auto plan = engine.plan(**reader_handle.value, action, nullptr, cancel);
        PB_CHECK(!plan.ok());
        PB_CHECK(has_rule(plan.findings, "E-INS-28"));
        PB_CHECK(compare(before, snapshot(rig.root())).identical);
    }
}

PB_TEST(an_installed_withdrawn_version_is_shown_but_never_removed_replaced_or_changed) {
    FakeReader reader = with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0"));
    EngineRig rig(reader);
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    e.withdrawn_versions = {"1.2.0"};          // the installed one is withdrawn, the newer one is fine
    e.listed_versions = {"1.2.0", "1.3.0"};
    rig.host.publish(100);
    rig.write_reader();
    Snapshot before = snapshot(rig.root());
    Engine& engine = rig.make_engine();
    auto reader_handle = engine.open_reader(rig.root());
    CancelToken cancel;
    auto listing = engine.list(**reader_handle.value, nullptr, cancel);
    PB_CHECK(listing.ok());
    const AppEntry* a = find(listing.value->apps, "testapp");
    PB_CHECK(a != nullptr);
    PB_CHECK(a->installed_version_withdrawn);
    PB_CHECK(a->update_available);                       // the offered 1.3.0 is a proper update
    // listing and planning change nothing on the reader
    Action update;
    update.type = Action::Type::Update;
    update.app_id = "testapp";
    auto plan = engine.plan(**reader_handle.value, update, nullptr, cancel);
    PB_CHECK(plan.ok());
    PB_CHECK(compare(before, snapshot(rig.root())).identical);
    // nothing in the plan removes the app because its version is withdrawn: it is an update, not a removal
    PB_CHECK(plan.value->plan.kind == PlanKind::Update);
    PB_CHECK(plan.value->plan.menu.action != MenuChange::Action::Remove);
}

PB_TEST(a_withdrawn_version_is_not_offered_even_when_it_is_the_only_one) {
    EngineRig rig;
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    CatalogEntry& e = rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    e.withdrawn_versions = {"1.2.0"};
    rig.host.publish(100);
    rig.write_reader();
    Engine& engine = rig.make_engine();
    auto reader_handle = engine.open_reader(rig.root());
    CancelToken cancel;
    auto listing = engine.list(**reader_handle.value, nullptr, cancel);
    PB_CHECK(listing.ok());
    const AppEntry* a = find(listing.value->apps, "testapp");
    PB_CHECK(a != nullptr);
    PB_CHECK(!a->offered);
}

PB_TEST_MAIN()
