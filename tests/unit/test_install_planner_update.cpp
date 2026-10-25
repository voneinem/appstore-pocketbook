// T085: the plan for updating an installed app to the offered version.
#include "install_fixtures.h"
#include "pbstore/install/planner.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

Identity known() { return Identity{std::string("1030"), std::string("6.11.1437")}; }

PlanResult plan_update(const FakeReader& reader, const SampleApp& offered, PlanKind kind = PlanKind::Update) {
    CrashFs mem;
    reader.load_into(mem);
    FatRulesFs fat(mem);
    Manifest m = manifest_of(offered);
    ScanRequest req;
    req.app_id = m.id;
    req.manifest = &m;
    ReaderSnapshot snap = scan(fat, known(), req);
    PlanRequest pr;
    pr.kind = kind;
    pr.app_id = m.id;
    pr.manifest = &m;
    pr.package_sha256 = sha256_hex(m.id + m.version);
    FixedClock clock;
    pr.now = clock.now();
    pr.local_now = clock.local_now();
    pr.installed_by = "pbstore-install 0.2.0";
    return make_plan(pr, snap);
}

FakeReader installed_v1() {
    FakeReader r;
    r.add_data_dir("testapp", {{"notes.db", "precious"}});
    return with_installed(r, nested_sample_app());
}

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
const PlanItem* item(const Plan& p, const std::string& path) {
    for (const PlanItem& i : p.items) if (i.path == path) return &i;
    return nullptr;
}

}  // namespace

PB_TEST(the_plan_marks_files_to_replace_to_add_and_to_remove) {
    PlanResult res = plan_update(installed_v1(), nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(res.plan.has_value());
    const Plan& p = *res.plan;
    PB_CHECK(p.kind == PlanKind::Update);
    PB_CHECK_EQ(p.from_version, std::string("1.2.0"));
    PB_CHECK_EQ(p.to_version, std::string("1.3.0"));
    PB_CHECK(item(p, "applications/testapp.bin") != nullptr);
    PB_CHECK(item(p, "applications/testapp.bin")->op == ItemOp::Replace);
    PB_CHECK(!item(p, "applications/testapp.bin")->old_sha256.empty());
    PB_CHECK(item(p, "applications/testapp/new.bin") != nullptr);
    PB_CHECK(item(p, "applications/testapp/new.bin")->op == ItemOp::Add);
    PB_CHECK(item(p, "applications/testapp/lib/extra/data.txt") != nullptr);
    PB_CHECK(item(p, "applications/testapp/lib/extra/data.txt")->op == ItemOp::Remove);   // only in the old version
    PB_CHECK(p.record_change == RecordChange::Replace);
    PB_CHECK(!p.record_old_sha256.empty());
    PB_CHECK(p.data.has_value());                                                         // the data folder stays
    PB_CHECK(p.data->exists);
    PB_CHECK(p.confirmations == (Confirmations{Confirmation::Plan}));
    const PlanItem* last_change = nullptr;
    for (const PlanItem& i : p.items) if (i.op != ItemOp::Keep) last_change = &i;
    PB_CHECK(last_change != nullptr && last_change->op == ItemOp::Remove);       // the files only in the old version come last
}

PB_TEST(a_newer_version_is_planned_and_an_equal_or_older_one_is_nothing_to_do) {
    FakeReader r = installed_v1();                                                        // 1.2.0
    PlanResult newer = plan_update(r, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(newer.plan.has_value());
    PB_CHECK_EQ(newer.plan->to_version, std::string("1.3.0"));
    PlanResult equal = plan_update(r, nested_sample_app("testapp", "1.2.0"));
    PB_CHECK(!equal.plan.has_value());
    PB_CHECK(equal.findings.empty());
    bool w01 = false;
    for (const auto& n : equal.notices) w01 = w01 || n.rule_id == "W-INS-01";
    PB_CHECK(w01);
    PlanResult older = plan_update(r, nested_sample_app_v2("testapp", "U_TestApp", "1.1.0"));
    PB_CHECK(!older.plan.has_value());
    PB_CHECK(older.findings.empty());
    PB_CHECK(!older.notices.empty());
}

PB_TEST(the_entry_changes_only_if_it_differs_and_a_group_move_needs_an_existing_group) {
    FakeReader r = installed_v1();
    PlanResult same_entry = plan_update(r, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(same_entry.plan.has_value());
    PB_CHECK(same_entry.plan->menu.action == MenuChange::Action::None);                 // same title, path, icons, group
    PB_CHECK(same_entry.plan->view_new_text.empty());
    PB_CHECK(same_entry.plan->backup.name.empty());                                      // nothing changes in the configuration: no backup
    SampleApp changed = nested_sample_app_v2("testapp", "U_TestApp", "1.3.0");
    pbstore::json::Value* menu = nullptr;
    for (auto& kv : changed.manifest.object) if (kv.first == "menu_entry") menu = &kv.second;
    menu->set("title", pbstore::json::Value::from_string("New Title"));
    PlanResult retitled = plan_update(r, changed);
    PB_CHECK(retitled.plan.has_value());
    PB_CHECK(retitled.plan->menu.action == MenuChange::Action::Replace);
    PB_CHECK(!retitled.plan->view_new_text.empty());
    PB_CHECK(!retitled.plan->backup.name.empty());
    menu->set("group", pbstore::json::Value::from_string("@Favorites"));
    PlanResult moved = plan_update(r, changed);
    PB_CHECK(moved.plan.has_value());
    PB_CHECK_EQ(moved.plan->menu.group, std::string("@Favorites"));
    menu->set("group", pbstore::json::Value::from_string("@Nowhere"));
    PlanResult missing = plan_update(r, changed);
    PB_CHECK(!missing.plan.has_value());
    PB_CHECK(has_rule(missing.findings, "E-INS-14"));
}

PB_TEST(files_changed_since_the_install_need_the_edited_files_confirmation) {
    FakeReader r = installed_v1();
    r.files["applications/testapp.bin"] = "the user edited this";                        // a file the update replaces
    r.files["applications/testapp/lib/extra/data.txt"] = "and this one, which the update drops";
    PlanResult res = plan_update(r, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(res.plan.has_value());
    PB_CHECK(res.plan->confirmations.count(Confirmation::EditedFiles) == 1);
    PB_CHECK(has(item(*res.plan, "applications/testapp.bin")->reason, "changed since the install"));
    PB_CHECK(has(item(*res.plan, "applications/testapp/lib/extra/data.txt")->reason, "changed since the install"));
    const std::string text = render_plan(*res.plan);
    PB_CHECK(has(text, "E-INS-32"));
    PB_CHECK(has(text, "applications/testapp.bin"));
    // a recorded file that was deleted by hand is simply put back
    FakeReader r2 = installed_v1();
    r2.files.erase("applications/testapp.bin");
    PlanResult res2 = plan_update(r2, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(res2.plan.has_value());
    PB_CHECK(item(*res2.plan, "applications/testapp.bin")->op == ItemOp::Add);
    PB_CHECK(res2.plan->confirmations.count(Confirmation::EditedFiles) == 0);
}

PB_TEST(an_app_installed_from_a_local_file_says_that_its_source_changes_to_the_catalog) {
    FakeReader r = installed_v1();
    std::string rec = r.files["system/pbstore/installed/testapp.json"];
    rec.replace(rec.find("\"catalog\""), 9, "\"local\"");
    r.files["system/pbstore/installed/testapp.json"] = rec;
    PlanResult res = plan_update(r, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(res.plan.has_value());
    const std::string text = render_plan(*res.plan);
    PB_CHECK(has(text, "local file"));
    PB_CHECK(has(text, "catalog"));
    PB_CHECK_EQ(res.plan->source, std::string("catalog"));
}

PB_TEST(space_counts_the_old_version_too_until_the_commit_point) {
    FakeReader r = installed_v1();
    PlanResult res = plan_update(r, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(res.plan.has_value());
    PB_CHECK(res.plan->space.parts.old_version > 0);
}

PB_TEST(an_app_that_is_not_installed_or_installed_by_hand_cannot_be_updated) {
    PlanResult none = plan_update(FakeReader(), nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(!none.plan.has_value());
    PB_CHECK(has_rule(none.findings, "E-INS-30"));
    FakeReader hand;
    hand.add_hand_installed(make_sample_app("testapp", "1.0.0", "U_TestApp", "system/testapp"));
    PlanResult by_hand = plan_update(hand, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    PB_CHECK(!by_hand.plan.has_value());
    PB_CHECK(has_rule(by_hand.findings, "E-INS-31"));
}

PB_TEST_MAIN()
