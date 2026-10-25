// T094: adopt writes only the record; replace needs the explicit confirmation and lists what changes; the
// list tells apart apps installed outside the installer from unknown ones.
#include "install_fixtures.h"
#include "pbstore/install/app_state.h"
#include "pbstore/install/planner.h"
#include "sample_catalog.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

Identity known() { return Identity{std::string("1030"), std::string("6.11.1437")}; }

PlanResult plan_for(const FakeReader& reader, const SampleApp& package, PlanKind kind, const std::string& id = "") {
    CrashFs mem;
    reader.load_into(mem);
    FatRulesFs fat(mem);
    Manifest m = manifest_of(package);
    ScanRequest req;
    req.app_id = id.empty() ? m.id : id;
    req.manifest = kind == PlanKind::Remove ? nullptr : &m;
    ReaderSnapshot snap = scan(fat, known(), req);
    PlanRequest pr;
    pr.kind = kind;
    pr.app_id = req.app_id;
    pr.manifest = req.manifest;
    pr.package_sha256 = sha256_hex(m.id + m.version);
    FixedClock clock;
    pr.now = clock.now();
    pr.local_now = clock.local_now();
    pr.installed_by = "pbstore-install 0.2.0";
    return make_plan(pr, snap);
}

FakeReader by_hand(const SampleApp& app) {
    FakeReader r;
    r.add_hand_installed(app);
    r.add_data_dir("testapp", {{"notes.db", "precious"}});
    return r;
}

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

}  // namespace

PB_TEST(adopting_writes_only_the_record_and_changes_no_other_byte) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    PlanResult res = plan_for(r, app, PlanKind::Adopt);
    PB_CHECK(res.plan.has_value());
    const Plan& p = *res.plan;
    PB_CHECK(p.kind == PlanKind::Adopt);
    PB_CHECK_EQ(p.source, std::string("adopted"));
    for (const PlanItem& i : p.items) PB_CHECK(i.op == ItemOp::Keep);                    // every file was compared and is equal
    PB_CHECK(p.view_new_text.empty());
    PB_CHECK(p.menu.action == MenuChange::Action::None);
    PB_CHECK(p.directories_to_create.empty());
    PB_CHECK(p.record_change == RecordChange::Write);
    RecordParse rp = parse_install_record(p.record_text, "testapp");
    PB_CHECK(rp.record.has_value());
    PB_CHECK_EQ(rp.record->source, std::string("adopted"));
    // the folders that hold nothing but the app's own files are recorded, so that a removal tidies them up;
    // shared folders (applications, applications/icons) never are
    PB_CHECK((rp.record->directories == std::vector<std::string>{"applications/testapp", "applications/testapp/lib", "applications/testapp/lib/extra"}));
    // the entry hash is that of the entry as it was found
    ParseResult cfg = parse_desktop_config(r.files["system/config/desktop/view.json"]);
    PB_CHECK_EQ(rp.record->entry_sha256, sha256_hex(entry_value_text(*cfg.config, "U_TestApp")));
    PB_CHECK(p.confirmations == (Confirmations{Confirmation::Plan, Confirmation::UnknownFirmware, Confirmation::UnknownModel}) ||
             p.confirmations == (Confirmations{Confirmation::Plan}));
    PB_CHECK(!has(render_plan(p), "backup"));
}

PB_TEST(a_folder_with_a_file_that_is_not_the_apps_is_not_recorded_as_the_apps) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    r.put_file("applications/testapp/lib/notes-of-the-user.txt", "mine");
    PlanResult res = plan_for(r, app, PlanKind::Adopt);
    PB_CHECK(res.plan.has_value());
    RecordParse rp = parse_install_record(res.plan->record_text, "testapp");
    PB_CHECK(rp.record.has_value());
    // lib holds a foreign file, so it and its parent stay out; the folder below it holds only the app's file
    PB_CHECK((rp.record->directories == std::vector<std::string>{"applications/testapp/lib/extra"}));
}

PB_TEST(files_that_differ_refuse_the_adoption_and_point_to_replace) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    r.files["applications/testapp.bin"][3] ^= 1;
    PlanResult res = plan_for(r, app, PlanKind::Adopt);
    PB_CHECK(!res.plan.has_value());
    PB_CHECK(has_rule(res.findings, "E-INS-20"));
    PB_CHECK(has(res.findings[0].message, "applications/testapp.bin"));
    PB_CHECK(has(res.findings[0].message, "replace"));
}

PB_TEST(replace_needs_its_confirmation_and_lists_the_changes) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    r.files["applications/testapp.bin"][3] ^= 1;
    r.files.erase("applications/icons/testapp_f.bmp");
    PlanResult res = plan_for(r, app, PlanKind::Replace);
    PB_CHECK(res.plan.has_value());
    const Plan& p = *res.plan;
    PB_CHECK(p.confirmations.count(Confirmation::ReplaceOutside) == 1);
    int replaced = 0, added = 0, kept = 0;
    for (const PlanItem& i : p.items) {
        if (i.op == ItemOp::Replace) { ++replaced; PB_CHECK_EQ(i.path, std::string("applications/testapp.bin")); PB_CHECK(!i.old_sha256.empty()); }
        if (i.op == ItemOp::Add) { ++added; PB_CHECK_EQ(i.path, std::string("applications/icons/testapp_f.bmp")); }
        if (i.op == ItemOp::Keep) ++kept;
    }
    PB_CHECK_EQ(replaced, 1);
    PB_CHECK_EQ(added, 1);
    PB_CHECK(kept > 0);
    PB_CHECK(p.record_change == RecordChange::Write);
    PB_CHECK_EQ(p.source, std::string("catalog"));
    PB_CHECK(has(render_plan(p), "E-INS-38"));
}

PB_TEST(replace_of_an_app_that_has_a_record_or_is_not_on_the_reader_is_refused) {
    SampleApp app = nested_sample_app();
    PlanResult recorded = plan_for(with_installed(FakeReader(), app), app, PlanKind::Replace);
    PB_CHECK(!recorded.plan.has_value());
    PB_CHECK(!recorded.findings.empty());
    PlanResult absent = plan_for(FakeReader(), app, PlanKind::Replace);
    PB_CHECK(!absent.plan.has_value());
    PB_CHECK(has_rule(absent.findings, "E-INS-30"));
}

PB_TEST(remove_of_an_app_installed_outside_the_installer_gives_e_ins_31) {
    SampleApp app = nested_sample_app();
    PlanResult res = plan_for(by_hand(app), app, PlanKind::Remove);
    PB_CHECK(!res.plan.has_value());
    PB_CHECK(has_rule(res.findings, "E-INS-31"));
}

PB_TEST(the_list_tells_outside_the_installer_from_unknown_and_ignores_vendor_entries) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    CrashFs mem;
    r.load_into(mem);
    FatRulesFs fat(mem);
    Catalog c = sample_catalog(100);
    c.apps.clear();
    CatalogEntry e = sample_entry("testapp", "1.2.0");
    e.menu_entry_id = "U_TestApp";
    c.apps.push_back(e);
    auto apps = derive_app_states(&c, scan(fat, known(), ScanRequest{}));
    int outside = 0, unknown = 0;
    for (const auto& a : apps) {
        if (a.id == "testapp") { PB_CHECK(a.state == AppStateKind::OutsideInstaller); ++outside; }
        if (a.id == "U_AlphaApp") { PB_CHECK(a.state == AppStateKind::UnknownApp); ++unknown; }
        PB_CHECK(a.id != "Dictionary");
    }
    PB_CHECK_EQ(outside, 1);
    PB_CHECK_EQ(unknown, 1);
}

PB_TEST_MAIN()
