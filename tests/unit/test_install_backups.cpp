// T044: the backup of the desktop configuration before every change.
#include "fixed_clock.h"
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
LocalTime local(int y, int mo, int d, int h, int mi, int s, int off) {
    LocalTime t;
    t.year = y; t.month = mo; t.day = d; t.hour = h; t.minute = mi; t.second = s; t.offset_minutes = off;
    return t;
}
void make_area(ReaderFs& fs) {
    for (const char* d : {"system/pbstore", "system/pbstore/backups"}) fs.make_dir(d);
}
}  // namespace

PB_TEST(the_name_is_local_date_time_and_offset) {
    PB_CHECK_EQ(backup_name(local(2026, 10, 25, 14, 5, 9, 120)), std::string("view-20261025T140509+0200.json"));
    PB_CHECK_EQ(backup_name(local(2026, 1, 2, 3, 4, 5, -330)), std::string("view-20260102T030405-0530.json"));
    PB_CHECK_EQ(backup_name(local(2026, 1, 2, 3, 4, 5, 0)), std::string("view-20260102T030405+0000.json"));
    PB_CHECK(is_backup_name("view-20261025T140509+0200.json"));
    PB_CHECK(!is_backup_name("view-20261025T140509.json"));
    PB_CHECK(!is_backup_name("view-20261025.json"));
    PB_CHECK(!is_backup_name("my-notes.txt"));
    PB_CHECK(!is_backup_name("view-2026102AT140509+0200.json"));
    PB_CHECK_EQ(backup_day("view-20261025T140509+0200.json"), std::string("20261025"));
    PB_CHECK_EQ(backup_day("other.txt"), std::string(""));
}

PB_TEST(a_backup_is_a_full_copy_read_back_and_equal) {
    CrashFs fs;
    FakeReader().load_into(fs);
    make_area(fs);
    const std::string view = reader_view_text(fs);
    PB_CHECK(write_backup(fs, "view-20261025T140509+0200.json", view));
    std::string got;
    PB_CHECK(fs.read_file("system/pbstore/backups/view-20261025T140509+0200.json", 1u << 20, &got));
    PB_CHECK_EQ(got, view);
}

PB_TEST(if_the_backup_cannot_be_written_the_configuration_is_not_changed) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    Snapshot before = without_installer_area(snapshot(v.fat));
    // every mutating call that is part of writing the backup fails in turn: the install ends without a change
    for (int k = 1; k <= 12; ++k) {
        Volume w(reader);
        Built bb = make_install_plan(w.fat, nested_sample_app());
        w.mem.fail_mutating_call(k);
        ExecResult res = run_execute(w.fat, bb);
        if (res.ok()) continue;
        PB_CHECK(!res.findings.empty());
        PB_CHECK(compare(before, without_installer_area(snapshot(w.fat))).identical);
        PB_CHECK_EQ(reader_view_text(w.fat), reader_view_text(v.fat));
    }
    // the backup file cannot be created at all
    Volume w(reader);
    Built bb = make_install_plan(w.fat, nested_sample_app());
    w.mem.make_dir("system/pbstore");
    w.mem.make_dir("system/pbstore/backups");
    w.mem.create_exclusive(std::string("system/pbstore/backups/") + bb.plan.backup.name, "occupied", true);   // the name is taken
    ExecResult res = run_execute(w.fat, bb);
    PB_CHECK(!res.ok());
    bool e37 = false;
    for (const auto& f : res.findings) e37 = e37 || f.rule_id == "E-INS-37";
    PB_CHECK(e37);
    PB_CHECK_EQ(reader_view_text(w.fat), reader_view_text(v.fat));
}

PB_TEST(the_backup_exists_before_the_first_byte_of_the_configuration_changes) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    const std::string original = reader_view_text(v.fat);
    TraceFs fs(v.fat);
    PB_CHECK(run_execute(fs, b).ok());
    bool backup_seen = false;
    for (const auto& op : fs.ops) {
        if (op.name == "rename" && op.a == "system/config/desktop/view.json" && !backup_seen) PB_FAIL_AT("the configuration was changed before the backup existed");
        if (op.name == "create" && op.a == std::string("system/pbstore/backups/") + b.plan.backup.name) backup_seen = true;
    }
    PB_CHECK(backup_seen);
    std::string copy;
    PB_CHECK(v.fat.read_file(std::string("system/pbstore/backups/") + b.plan.backup.name, 1u << 20, &copy));
    PB_CHECK_EQ(copy, original);
}

PB_TEST(only_the_newest_backup_of_a_local_day_is_kept_and_other_files_are_never_touched) {
    CrashFs fs;
    FakeReader().load_into(fs);
    make_area(fs);
    fs.create_exclusive("system/pbstore/backups/view-20261025T080000+0200.json", "old1", true);
    fs.create_exclusive("system/pbstore/backups/view-20261025T100000+0200.json", "old2", true);
    fs.create_exclusive("system/pbstore/backups/view-20261024T230000+0200.json", "yesterday", true);
    fs.create_exclusive("system/pbstore/backups/my-own-notes.txt", "keep me", true);
    fs.create_exclusive("system/pbstore/backups/view-20261025.json", "odd name", true);
    BackupPlan p = plan_backup(fs, local(2026, 10, 25, 14, 0, 0, 120));
    PB_CHECK_EQ(p.name, std::string("view-20261025T140000+0200.json"));
    std::sort(p.delete_after_commit.begin(), p.delete_after_commit.end());
    PB_CHECK_EQ(p.delete_after_commit, (std::vector<std::string>{"view-20261025T080000+0200.json", "view-20261025T100000+0200.json"}));
    // the old ones are deleted only after the commit, and only by name pattern
    PB_CHECK_EQ(delete_old_backups(fs, {"view-20261025T080000+0200.json", "my-own-notes.txt", "../escape", "view-20261025.json"}), 1);
    PB_CHECK(!fs.stat("system/pbstore/backups/view-20261025T080000+0200.json").exists());
    PB_CHECK(fs.stat("system/pbstore/backups/my-own-notes.txt").exists());
    PB_CHECK(fs.stat("system/pbstore/backups/view-20261025.json").exists());
    PB_CHECK(fs.stat("system/pbstore/backups/view-20261025T100000+0200.json").exists());
    PB_CHECK(fs.stat("system/pbstore/backups/view-20261024T230000+0200.json").exists());
}

PB_TEST(an_older_backup_of_the_day_survives_until_the_install_is_committed) {
    FakeReader reader;
    CrashFs mem;
    reader.load_into(mem);
    FatRulesFs fat(mem);
    make_area(fat);
    fat.create_exclusive("system/pbstore/backups/view-20261025T080000+0200.json", "old1", true);
    SampleApp app = nested_sample_app();
    Built b = make_install_plan(fat, app);
    PB_CHECK_EQ(b.plan.backup.delete_after_commit.size(), size_t(1));
    TraceFs fs(fat);
    PB_CHECK(run_execute(fs, b).ok());
    size_t removal = 0, commit = 0, new_backup = 0;
    for (size_t i = 0; i < fs.ops.size(); ++i) {
        const auto& op = fs.ops[i];
        if (op.name == "remove_file" && op.a == "system/pbstore/backups/view-20261025T080000+0200.json") removal = i;
        if (op.name == "rename" && op.b == std::string(kJournalDir) + "/" + kCommitName) commit = i;
        if (op.name == "create" && op.a == std::string("system/pbstore/backups/") + b.plan.backup.name) new_backup = i;
    }
    PB_CHECK(removal > 0 && new_backup > 0 && commit > 0);
    PB_CHECK(removal > commit);          // deleted only after the commit
    PB_CHECK(removal > new_backup);      // and after the new one was written
    PB_CHECK(!fat.stat("system/pbstore/backups/view-20261025T080000+0200.json").exists());
    PB_CHECK(fat.stat(std::string("system/pbstore/backups/") + b.plan.backup.name).exists());
}

PB_TEST(a_day_boundary_and_the_offset_decide_which_backups_are_the_same_day) {
    CrashFs fs;
    FakeReader().load_into(fs);
    make_area(fs);
    fs.create_exclusive("system/pbstore/backups/view-20261024T235959+0200.json", "late yesterday", true);
    fs.create_exclusive("system/pbstore/backups/view-20261025T000000+0200.json", "start of today", true);
    BackupPlan just_after_midnight = plan_backup(fs, local(2026, 10, 25, 0, 0, 1, 120));
    PB_CHECK_EQ(just_after_midnight.delete_after_commit, (std::vector<std::string>{"view-20261025T000000+0200.json"}));
    // the day after the change from summer to winter time: the same calendar day with another offset
    BackupPlan dst = plan_backup(fs, local(2026, 10, 25, 23, 30, 0, 60));
    PB_CHECK_EQ(dst.name, std::string("view-20261025T233000+0100.json"));
    PB_CHECK_EQ(dst.delete_after_commit, (std::vector<std::string>{"view-20261025T000000+0200.json"}));
}

PB_TEST_MAIN()
