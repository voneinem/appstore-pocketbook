// T076: the plan for removing an app: only recorded files whose hash still equals the record, the entry, the
// record; changed files stay and are named; the data folder stays.
#include "install_fixtures.h"
#include "pbstore/install/planner.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

Identity known() { return Identity{std::string("1030"), std::string("6.11.1437")}; }

PlanResult plan_remove(const FakeReader& reader, const std::string& app_id) {
    CrashFs mem;
    reader.load_into(mem);
    FatRulesFs fat(mem);
    ScanRequest req;
    req.app_id = app_id;
    ReaderSnapshot snap = scan(fat, known(), req);
    PlanRequest pr;
    pr.kind = PlanKind::Remove;
    pr.app_id = app_id;
    FixedClock clock;
    pr.now = clock.now();
    pr.local_now = clock.local_now();
    pr.installed_by = "pbstore-install 0.2.0";
    return make_plan(pr, snap);
}

FakeReader installed_with_data() {
    FakeReader r;
    r.add_data_dir("testapp", {{"notes.db", "12345"}, {"sub/b.db", "123"}});
    return with_installed(r, nested_sample_app());
}

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

}  // namespace

PB_TEST(removal_lists_exactly_the_recorded_files_the_entry_the_record_and_keeps_the_data_folder) {
    FakeReader r = installed_with_data();
    PlanResult res = plan_remove(r, "testapp");
    PB_CHECK(res.plan.has_value());
    PB_CHECK(res.findings.empty());
    const Plan& p = *res.plan;
    PB_CHECK(p.kind == PlanKind::Remove);
    SampleApp app = nested_sample_app();
    std::set<std::string> expected;
    for (const auto& f : app.files) expected.insert(f.name);
    std::set<std::string> got;
    for (const PlanItem& i : p.items) {
        PB_CHECK(i.op == ItemOp::Remove);
        got.insert(i.path);
        PB_CHECK(!i.old_sha256.empty());
    }
    PB_CHECK(got == expected);
    PB_CHECK(p.menu.action == MenuChange::Action::Remove);
    PB_CHECK_EQ(p.menu.id, std::string("U_TestApp"));
    PB_CHECK(!p.view_new_text.empty());
    PB_CHECK(!parse_desktop_config(p.view_new_text).config->has_entry("U_TestApp"));
    PB_CHECK(parse_desktop_config(p.view_new_text).config->has_entry("U_AlphaApp"));     // the others stay
    PB_CHECK(p.record_change == RecordChange::Delete);
    PB_CHECK(p.data.has_value());
    PB_CHECK_EQ(p.data->path, std::string("system/testapp"));
    PB_CHECK_EQ(p.data->files, uint64_t(2));
    PB_CHECK_EQ(p.data->bytes, uint64_t(8));
    PB_CHECK(p.confirmations == (Confirmations{Confirmation::Plan}));
    // created folders are removed when they are empty, deepest first
    PB_CHECK_EQ(p.directories_to_remove, (std::vector<std::string>{"applications/testapp/lib/extra", "applications/testapp/lib", "applications/testapp"}));
    const std::string text = render_plan(p);
    PB_CHECK(has(text, "system/testapp"));
    PB_CHECK(has(text, "kept"));
    PB_CHECK(has(text, "2 files"));
    for (const PlanItem& i : p.items) PB_CHECK(has(text, i.path));
}

PB_TEST(files_changed_since_the_install_stay_and_are_named_and_deleted_ones_are_skipped) {
    FakeReader r = installed_with_data();
    r.files["applications/testapp.bin"] = "edited by the user";
    r.files.erase("applications/icons/testapp_f.bmp");
    PlanResult res = plan_remove(r, "testapp");
    PB_CHECK(res.plan.has_value());
    std::set<std::string> removed, kept;
    for (const PlanItem& i : res.plan->items) (i.op == ItemOp::Remove ? removed : kept).insert(i.path);
    PB_CHECK(kept == std::set<std::string>{"applications/testapp.bin"});
    PB_CHECK(removed.count("applications/testapp.bin") == 0);
    PB_CHECK(removed.count("applications/icons/testapp_f.bmp") == 0);      // already gone: nothing to do
    PB_CHECK(removed.count("applications/testapp.app") == 1);
    const std::string text = render_plan(*res.plan);
    PB_CHECK(has(text, "applications/testapp.bin"));
    PB_CHECK(has(text, "changed since the install"));
    PB_CHECK(has(text, "stays"));
}

PB_TEST(an_app_that_is_not_installed_gives_e_ins_30) {
    FakeReader r;
    PlanResult res = plan_remove(r, "testapp");
    PB_CHECK(!res.plan.has_value());
    PB_CHECK(has_rule(res.findings, "E-INS-30"));
}

PB_TEST(an_app_without_a_record_gives_e_ins_31_and_says_to_adopt_or_replace_first) {
    FakeReader r;
    r.add_hand_installed(make_sample_app("testapp", "1.2.0", "U_TestApp", "system/testapp"));
    PlanResult res = plan_remove(r, "testapp");
    PB_CHECK(!res.plan.has_value());
    PB_CHECK(has_rule(res.findings, "E-INS-31"));
    PB_CHECK(has(res.findings[0].message, "adopt"));
    PB_CHECK(has(res.findings[0].message, "replace"));
}

PB_TEST(an_entry_changed_since_the_install_is_removed_anyway_with_a_notice) {
    FakeReader r = installed_with_data();
    ParseResult p = parse_desktop_config(r.files["system/config/desktop/view.json"]);
    EntryValue changed = {"/mnt/ext1/applications/testapp.app", "Renamed by the reader", "applications/icons/testapp.bmp", "applications/icons/testapp_f.bmp"};
    r.files["system/config/desktop/view.json"] = *edit(*p.config, replace_entry("U_TestApp", changed, "@General")).text;
    PlanResult res = plan_remove(r, "testapp");
    PB_CHECK(res.plan.has_value());
    bool w02 = false;
    for (const auto& n : res.notices) w02 = w02 || n.rule_id == "W-INS-02";
    PB_CHECK(w02);
    PB_CHECK(res.plan->menu.action == MenuChange::Action::Remove);
    // an entry that is no longer in the file at all
    FakeReader r2 = installed_with_data();
    ParseResult p2 = parse_desktop_config(r2.files["system/config/desktop/view.json"]);
    r2.files["system/config/desktop/view.json"] = *edit(*p2.config, remove_entry("U_TestApp")).text;
    PlanResult res2 = plan_remove(r2, "testapp");
    PB_CHECK(res2.plan.has_value());
    PB_CHECK(res2.plan->menu.action == MenuChange::Action::None);
    PB_CHECK(res2.plan->view_new_text.empty());
    PB_CHECK(res2.plan->backup.name.empty());
}

PB_TEST_MAIN()
