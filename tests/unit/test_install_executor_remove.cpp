// T077: the executor, kind remove.
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
FakeReader installed_with_data() {
    FakeReader r;
    r.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
    return with_installed(r, nested_sample_app());
}
bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
}  // namespace

PB_TEST(removal_takes_the_entry_the_recorded_files_and_the_record_and_leaves_everything_else) {
    FakeReader r = installed_with_data();
    Volume v(r);
    Snapshot before = snapshot(v.fat);
    Built b = make_planned(v.fat, nested_sample_app(), PlanKind::Remove);
    PB_CHECK(run_execute(v.fat, b).ok());
    // the app is gone
    for (const auto& f : nested_sample_app().files) PB_CHECK(!v.fat.stat(f.name).exists());
    PB_CHECK(!v.fat.stat("applications/testapp").exists());                 // its created folders are gone
    PB_CHECK(!v.fat.stat("system/pbstore/installed/testapp.json").exists());
    PB_CHECK(!parse_desktop_config(reader_view_text(v.fat)).config->has_entry("U_TestApp"));
    PB_CHECK(!journal_exists(v.fat));
    // everything else is byte-identical, the data folder in particular
    Diff d = compare(before, snapshot(v.fat));
    for (const std::string& p : d.added) PB_CHECK(in_installer_area(p));
    for (const std::string& p : d.changed) PB_CHECK(p == "system/config/desktop/view.json");
    for (const std::string& p : d.removed) PB_CHECK(starts_with(p, "applications/testapp") || starts_with(p, "applications/icons/testapp") || p == "system/pbstore/installed/testapp.json" || in_installer_area(p));
    std::string notes;
    PB_CHECK(v.fat.read_file("system/testapp/notes.db", 100, &notes));
    PB_CHECK_EQ(notes, std::string("precious"));
    PB_CHECK(v.fat.stat("system/testapp/sub/more.db").exists());
    PB_CHECK(v.fat.stat("applications/alpha.app").exists());
}

PB_TEST(the_order_is_the_entry_first_then_the_files_then_the_record_then_the_empty_folders) {
    FakeReader r = installed_with_data();
    Volume v(r);
    Built b = make_planned(v.fat, nested_sample_app(), PlanKind::Remove);
    TraceFs fs(v.fat);
    PB_CHECK(run_execute(fs, b).ok());
    std::vector<std::string> live;
    for (const auto& op : fs.ops) {
        if (!op.mutating) continue;
        if (op.name == "rename" && op.a == "system/config/desktop/view.json") live.push_back("view");
        else if (op.name == "rename" && starts_with(op.a, "applications/")) live.push_back("file " + op.a);
        else if (op.name == "rename" && op.a == "system/pbstore/installed/testapp.json") live.push_back("record");
        else if (op.name == "remove_dir" && starts_with(op.a, "applications/")) live.push_back("dir " + op.a);
    }
    PB_CHECK(live.size() > 6);
    PB_CHECK_EQ(live.front(), std::string("view"));
    size_t last_file = 0, record = 0, first_dir = live.size();
    for (size_t i = 0; i < live.size(); ++i) {
        if (starts_with(live[i], "file ")) last_file = i;
        if (live[i] == "record") record = i;
        if (starts_with(live[i], "dir ") && first_dir == live.size()) first_dir = i;
    }
    PB_CHECK(last_file < record);
    PB_CHECK(record < first_dir);
    // no copying of data between the first and the last change to the live area
    long first = -1, last = -1;
    for (size_t i = 0; i < fs.ops.size(); ++i) {
        const auto& op = fs.ops[i];
        if (!op.mutating) continue;
        const bool liveop = (op.name == "rename" && (!in_installer_area(op.a) || !in_installer_area(op.b))) || (op.name == "remove_dir" && !in_installer_area(op.a));
        if (liveop) { if (first < 0) first = static_cast<long>(i); last = static_cast<long>(i); }
    }
    for (long i = first; i <= last; ++i) PB_CHECK(fs.ops[static_cast<size_t>(i)].name != "create");
}

PB_TEST(changed_files_and_folders_with_other_content_stay) {
    FakeReader r = installed_with_data();
    r.files["applications/testapp.bin"] = "edited by the user";
    r.files["applications/testapp/lib/mine.txt"] = "a file the user added";
    Volume v(r);
    Built b = make_planned(v.fat, nested_sample_app(), PlanKind::Remove);
    PB_CHECK(run_execute(v.fat, b).ok());
    std::string c;
    PB_CHECK(v.fat.read_file("applications/testapp.bin", 100, &c));
    PB_CHECK_EQ(c, std::string("edited by the user"));
    PB_CHECK(v.fat.read_file("applications/testapp/lib/mine.txt", 100, &c));
    PB_CHECK(v.fat.stat("applications/testapp/lib").exists());      // not empty: stays
    PB_CHECK(!v.fat.stat("applications/testapp.app").exists());
}

PB_TEST(a_menu_entry_the_user_changed_is_removed_by_its_id) {
    FakeReader r = installed_with_data();
    ParseResult p = parse_desktop_config(r.files["system/config/desktop/view.json"]);
    EntryValue changed = {"/mnt/ext1/applications/testapp.app", "My own title", "i.bmp", "f.bmp"};
    r.files["system/config/desktop/view.json"] = *edit(*p.config, replace_entry("U_TestApp", changed, "@General")).text;
    Volume v(r);
    Built b = make_planned(v.fat, nested_sample_app(), PlanKind::Remove);
    PB_CHECK(run_execute(v.fat, b).ok());
    PB_CHECK(!parse_desktop_config(reader_view_text(v.fat)).config->has_entry("U_TestApp"));
}

PB_TEST_MAIN()
