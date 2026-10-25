// T058: the pure planner, kind install: what the plan lists, and the refusals and confirmations.
#include "install_fixtures.h"
#include "pbstore/install/planner.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

Identity known() { return Identity{std::string("1030"), std::string("6.11.1437")}; }

struct Setup {
    FakeReader reader;
    SampleApp app = nested_sample_app();
    Manifest manifest = manifest_of(app);
    Identity identity = known();
    std::function<void(CrashFs&)> tweak;
    void edit_manifest(const std::function<void(Manifest&)>& f) { f(manifest); }

    // scan the reader (after tweaks on the in-memory volume) and plan an installation
    PlanResult plan(PlanKind kind = PlanKind::Install) {
        CrashFs mem;
        reader.load_into(mem);
        if (tweak) tweak(mem);
        FatRulesFs fat(mem);
        ScanRequest req;
        req.app_id = manifest.id;
        req.manifest = &manifest;
        ReaderSnapshot snap = scan(fat, identity, req);
        PlanRequest pr;
        pr.kind = kind;
        pr.app_id = manifest.id;
        pr.manifest = &manifest;
        pr.package_sha256 = sha256_hex(manifest.id + manifest.version);
        pr.catalog_generated_at = "2026-10-24T12:00:00Z";
        FixedClock clock;
        pr.now = clock.now();
        pr.local_now = clock.local_now();
        pr.installed_by = "pbstore-install 0.2.0";
        return make_plan(pr, snap);
    }
};

bool has_rule(const PlanResult& r, const char* id) {
    for (const auto& f : r.findings) if (f.rule_id == id) return true;
    return false;
}
const Finding* rule(const PlanResult& r, const char* id) {
    for (const auto& f : r.findings) if (f.rule_id == id) return &f;
    return nullptr;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

}  // namespace

PB_TEST(the_plan_lists_files_directories_menu_entry_data_folder_space_and_the_catalog_date) {
    Setup s;
    PlanResult r = s.plan();
    PB_CHECK(r.plan.has_value());
    PB_CHECK(r.findings.empty());
    const Plan& p = *r.plan;
    PB_CHECK(p.kind == PlanKind::Install);
    PB_CHECK_EQ(p.app_id, std::string("testapp"));
    PB_CHECK_EQ(p.to_version, std::string("1.2.0"));
    PB_CHECK_EQ(p.items.size(), s.manifest.files.size());
    for (const PlanItem& it : p.items) {
        PB_CHECK(it.op == ItemOp::Add);
        bool found = false;
        for (const auto& f : s.manifest.files) if (f.path == it.path) { found = true; PB_CHECK_EQ(it.size, uint64_t(f.size)); PB_CHECK_EQ(it.sha256, f.sha256); }
        PB_CHECK(found);
    }
    PB_CHECK_EQ(p.items.back().path, s.manifest.menu_entry.path);                 // the launcher last
    PB_CHECK_EQ(p.directories_to_create, (std::vector<std::string>{"applications/testapp", "applications/testapp/lib", "applications/testapp/lib/extra"}));
    PB_CHECK(p.menu.action == MenuChange::Action::Add);
    PB_CHECK_EQ(p.menu.id, std::string("U_TestApp"));
    PB_CHECK_EQ(p.menu.group, std::string("@General"));
    PB_CHECK(p.record_change == RecordChange::Write);
    PB_CHECK(!p.view_new_text.empty());
    PB_CHECK(p.data.has_value());
    PB_CHECK_EQ(p.data->path, std::string("system/testapp"));
    PB_CHECK(!p.data->exists);
    PB_CHECK(p.space.needed > p.space.parts.margin);
    PB_CHECK(p.space.free_bytes >= p.space.needed);
    PB_CHECK_EQ(p.catalog_generated_at, std::string("2026-10-24T12:00:00Z"));
    PB_CHECK(!p.backup.name.empty());
    PB_CHECK(p.confirmations == (Confirmations{Confirmation::Plan}));
    PB_CHECK_EQ(p.fingerprint.size(), size_t(64));
    // the rendered text names all of it
    const std::string text = render_plan(p);
    for (const PlanItem& it : p.items) { PB_CHECK(has(text, it.path)); PB_CHECK(has(text, it.sha256.substr(0, 12))); }
    PB_CHECK(has(text, "U_TestApp"));
    PB_CHECK(has(text, "@General"));
    PB_CHECK(has(text, "system/testapp"));
    PB_CHECK(has(text, "2026-10-24"));
    PB_CHECK(has(text, "MiB"));
}

PB_TEST(the_same_input_gives_the_same_plan_twice) {
    Setup s;
    PlanResult a = s.plan(), b = s.plan();
    PB_CHECK(a.plan && b.plan);
    PB_CHECK_EQ(render_plan(*a.plan), render_plan(*b.plan));
    PB_CHECK_EQ(a.plan->fingerprint, b.plan->fingerprint);
    PB_CHECK_EQ(a.plan->view_new_text, b.plan->view_new_text);
    PB_CHECK_EQ(a.plan->record_text, b.plan->record_text);
}

PB_TEST(a_missing_menu_group_gives_e_ins_14_and_lists_the_groups_that_exist) {
    Setup s;
    s.edit_manifest([](Manifest& m) { m.menu_entry.group = "@Nowhere"; });
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    const Finding* f = rule(r, "E-INS-14");
    PB_CHECK(f != nullptr);
    PB_CHECK(has(f->message, "@General"));
    PB_CHECK(has(f->message, "Favorites"));
}

PB_TEST(a_menu_entry_id_used_by_another_app_gives_e_ins_15_naming_both) {
    Setup s;
    s.edit_manifest([](Manifest& m) { m.menu_entry.id = "U_AlphaApp"; });          // an entry of the fake reader, no record
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    const Finding* f = rule(r, "E-INS-15");
    PB_CHECK(f != nullptr);
    PB_CHECK(has(f->message, "U_AlphaApp"));
    PB_CHECK(has(f->message, "testapp"));
    // an installed app by record
    Setup t;
    t.reader = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_TestApp"));
    PlanResult r2 = t.plan();
    PB_CHECK(!r2.plan.has_value());
    const Finding* g = rule(r2, "E-INS-15");
    PB_CHECK(g != nullptr);
    PB_CHECK(has(g->message, "otherapp"));
    PB_CHECK(has(g->message, "testapp"));
}

PB_TEST(entry_ids_that_differ_in_letter_case_are_two_ids_but_data_folders_are_one_claim) {
    Setup s;
    s.reader = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_notes"));
    s.edit_manifest([](Manifest& m) { m.menu_entry.id = "U_Notes"; });
    s.edit_manifest([](Manifest& m) { m.data_dir = "system/otherdata"; });
    PlanResult r = s.plan();
    PB_CHECK(r.plan.has_value());           // U_Notes and U_notes are two entries: no conflict
    Setup t;
    t.reader = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_OtherApp"));   // data_dir system/otherapp
    t.edit_manifest([](Manifest& m) { m.data_dir = "system/OtherApp"; });
    PlanResult r2 = t.plan();
    PB_CHECK(!r2.plan.has_value());
    PB_CHECK(has_rule(r2, "E-INS-16"));
    PB_CHECK(has(rule(r2, "E-INS-16")->message, "otherapp"));
}

PB_TEST(a_data_folder_claimed_by_another_installed_app_gives_e_ins_16) {
    Setup s;
    s.reader = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_OtherApp"));
    s.edit_manifest([](Manifest& m) { m.data_dir = "system/otherapp"; });
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    PB_CHECK(has_rule(r, "E-INS-16"));
}

PB_TEST(a_reserved_data_folder_and_the_installers_own_area_give_e_ins_18) {
    for (const char* bad : {"system/config", "system/pbstore", "system/fonts"}) {
        Setup s;
        s.edit_manifest([&](Manifest& m) { m.data_dir = bad; });
        PlanResult r = s.plan();
        PB_CHECK(!r.plan.has_value());
        if (!has_rule(r, "E-INS-18")) PB_FAIL_AT(std::string("no E-INS-18 for ") + bad);
    }
}

PB_TEST(a_path_outside_the_allowed_set_gives_e_ins_19) {
    for (const char* bad : {"system/evil.bin", "system/pbstore/lock", "applications/../escape", "/etc/passwd", "applications"}) {
        Setup s;
        s.edit_manifest([&](Manifest& m) { m.files[0].path = bad; });
        PlanResult r = s.plan();
        PB_CHECK(!r.plan.has_value());
        if (!has_rule(r, "E-INS-19")) PB_FAIL_AT(std::string("no E-INS-19 for ") + bad);
    }
}

PB_TEST(a_file_in_the_way_gives_e_ins_20_also_when_it_differs_only_by_letter_case) {
    Setup s;
    s.reader.put_file("applications/testapp.app", "somebody's file");
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    PB_CHECK(has_rule(r, "E-INS-20"));
    PB_CHECK(has(rule(r, "E-INS-20")->message, "applications/testapp.app"));
    Setup t;
    t.reader.put_file("applications/TESTAPP.APP", "case variant");
    PlanResult r2 = t.plan();
    PB_CHECK(!r2.plan.has_value());
    PB_CHECK(has_rule(r2, "E-INS-20"));
    PB_CHECK(has(rule(r2, "E-INS-20")->message, "letter case"));
    Setup u;
    u.reader.put_file("applications/testapp", "a file where a folder is needed");
    PlanResult r3 = u.plan();
    PB_CHECK(!r3.plan.has_value());
    PB_CHECK(has_rule(r3, "E-INS-20"));
}

PB_TEST(a_path_owned_by_another_apps_record_gives_e_ins_21) {
    Setup s;
    SampleApp other = nested_sample_app("otherapp", "1.0.0", "U_OtherApp");
    s.reader = with_installed(FakeReader(), other);
    // the new package claims a file the other app placed
    s.edit_manifest([](Manifest& m) { m.files[0].path = "applications/otherapp.bin"; });
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    PB_CHECK(has_rule(r, "E-INS-21"));
    PB_CHECK(has(rule(r, "E-INS-21")->message, "otherapp"));
}

PB_TEST(a_link_like_object_on_a_target_path_gives_e_ins_22) {
    Setup s;
    s.tweak = [](CrashFs& mem) { mem.plant_link("applications/testapp.app"); };
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    PB_CHECK(has_rule(r, "E-INS-22"));
    Setup t;
    t.tweak = [](CrashFs& mem) { mem.plant_link("applications/icons"); };   // a link in the folder chain
    PlanResult r2 = t.plan();
    PB_CHECK(!r2.plan.has_value());
    PB_CHECK(has_rule(r2, "E-INS-22"));
}

PB_TEST(not_enough_space_gives_e_ins_07_with_needed_and_free) {
    Setup s;
    s.tweak = [](CrashFs& mem) { mem.set_space(5 * 1024 * 1024, 4096, true, false); };
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    PB_CHECK(has_rule(r, "E-INS-07"));
    PB_CHECK(has(rule(r, "E-INS-07")->message, "MiB"));
}

PB_TEST(another_model_or_too_old_a_firmware_is_refused) {
    Setup s;
    s.identity = Identity{std::string("1040"), std::string("6.11.1437")};
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    PB_CHECK(has_rule(r, "E-INS-08"));
    Setup t;
    t.edit_manifest([](Manifest& m) { m.min_firmware = "6.12.0"; });
    PlanResult r2 = t.plan();
    PB_CHECK(!r2.plan.has_value());
    PB_CHECK(has_rule(r2, "E-INS-10"));
    PB_CHECK(has(rule(r2, "E-INS-10")->message, "6.11.1437"));
    PB_CHECK(has(rule(r2, "E-INS-10")->message, "6.12.0"));
}

PB_TEST(unknown_model_and_firmware_need_their_confirmations_named_together_in_one_plan) {
    Setup s;
    s.identity = Identity{};
    PlanResult r = s.plan();
    PB_CHECK(r.plan.has_value());
    PB_CHECK(r.plan->confirmations.count(Confirmation::UnknownModel) == 1);
    PB_CHECK(r.plan->confirmations.count(Confirmation::UnknownFirmware) == 1);
    PB_CHECK(r.plan->confirmations.count(Confirmation::Plan) == 1);
    const std::string text = render_plan(*r.plan);
    PB_CHECK(has(text, "E-INS-09"));
    PB_CHECK(has(text, "E-INS-11"));
    PB_CHECK(has(text, "model"));
    PB_CHECK(has(text, "firmware"));
}

PB_TEST(an_existing_data_folder_without_an_owner_needs_a_confirmation_and_its_contents_stay_untouched) {
    Setup s;
    s.reader.add_data_dir("testapp", {{"a.db", "1234"}, {"sub/b.db", "12"}});
    PlanResult r = s.plan();
    PB_CHECK(r.plan.has_value());
    PB_CHECK(r.plan->confirmations.count(Confirmation::ExistingDataFolder) == 1);
    PB_CHECK(r.plan->data->exists);
    PB_CHECK_EQ(r.plan->data->files, uint64_t(2));
    PB_CHECK_EQ(r.plan->data->bytes, uint64_t(6));
    const std::string text = render_plan(*r.plan);
    PB_CHECK(has(text, "E-INS-17"));
    PB_CHECK(has(text, "2 files"));
    PB_CHECK(has(text, "stay"));
    for (const PlanItem& it : r.plan->items) PB_CHECK(it.path.compare(0, 14, "system/testapp") != 0);
}

PB_TEST(findings_of_the_desktop_configuration_stop_the_plan) {
    Setup s;
    s.reader.files["system/config/desktop/view.json"] = "{ broken";
    PlanResult r = s.plan();
    PB_CHECK(!r.plan.has_value());
    PB_CHECK(has_rule(r, "E-INS-12"));
}

PB_TEST(an_app_that_is_already_installed_cannot_be_installed_again) {
    Setup s;
    s.reader = with_installed(FakeReader(), nested_sample_app());
    PlanResult r = s.plan(PlanKind::Install);
    PB_CHECK(!r.plan.has_value());
    PB_CHECK(r.findings.empty());                       // the same version: nothing to do, which is not an error
    bool nothing = false;
    for (const auto& n : r.notices) nothing = nothing || n.rule_id == "W-INS-01";
    PB_CHECK(nothing);
    Setup t;
    t.reader = with_installed(FakeReader(), nested_sample_app("testapp", "1.0.0"));
    PlanResult r2 = t.plan(PlanKind::Install);          // another version: update is the way
    PB_CHECK(!r2.plan.has_value());
    PB_CHECK(has_rule(r2, "E-INS-15"));
}

PB_TEST_MAIN()
