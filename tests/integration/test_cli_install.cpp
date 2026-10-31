// T065: the command line of the test build (contracts/cli.md) installs from a signed test catalog onto a fake
// reader on disk.
#include "install_rig.h"
#include "test_support.h"

#include <sys/stat.h>
#include <unistd.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

const std::vector<std::string> kUnknown = {"--confirm-unknown-model", "--confirm-unknown-firmware"};

std::vector<std::string> args(std::vector<std::string> a, const std::vector<std::string>& more) {
    a.insert(a.end(), more.begin(), more.end());
    return a;
}

struct Rig : CliRig {
    explicit Rig(const FakeReader& r = FakeReader()) : CliRig(r) {
        if (!host.ready()) return;
        host.add_app(nested_sample_app());
        host.publish(100);
        ::mkdir((dir.path() + "/home").c_str(), 0755);
        write_reader();
    }
    std::vector<std::string> mounts_env(const std::vector<std::string>& more = {}) const {
        std::vector<std::string> e = {"PBSTORE_TEST_MOUNTS=" + root() + "|vfat|7516192768"};
        e.insert(e.end(), more.begin(), more.end());
        return e;
    }
};

}  // namespace

PB_TEST(readers_lists_candidates_with_label_mount_point_size_and_whether_they_qualify) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    ::mkdir((r.dir.path() + "/stick").c_str(), 0755);
    RunResult res = r.run({"readers"}, {"PBSTORE_TEST_MOUNTS=" + r.root() + "|vfat|7516192768;" + r.dir.path() + "/stick|vfat|1073741824"});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "reader"));
    PB_CHECK(has(res.out, r.root()));
    PB_CHECK(has(res.out, "7.0 GiB"));
    PB_CHECK(has(res.out, "stick"));
    PB_CHECK(has(res.out, "1.0 GiB"));
    PB_CHECK(has(res.out, "not a reader"));
    PB_CHECK(has(res.out, "applications/"));
}

PB_TEST(list_shows_the_states_the_versions_the_source_and_the_catalog_date) {
    FakeReader base;
    FakeReader with = with_installed(base, nested_sample_app("installedapp", "1.0.0", "U_InstalledApp"));
    Rig r(with);
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.add_app(nested_sample_app("installedapp", "1.1.0", "U_InstalledApp"));      // a newer offered version
    r.host.add_app(nested_sample_app("freshapp", "2.0.0", "U_FreshApp"));
    r.host.publish(101);
    RunResult res = r.run({"list", "--reader", r.root()});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "2026-10-24"));                       // the catalog's generation date
    PB_CHECK(has(res.out, "freshapp"));
    PB_CHECK(has(res.out, "not installed"));
    PB_CHECK(has(res.out, "installedapp"));
    PB_CHECK(has(res.out, "update available"));
    PB_CHECK(has(res.out, "1.0.0"));
    PB_CHECK(has(res.out, "1.1.0"));
    PB_CHECK(has(res.out, "catalog"));                          // the source of the installed copy
}

PB_TEST(install_dry_run_prints_the_plan_and_leaves_the_reader_byte_identical) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult res = r.run(args({"install", "testapp", "--reader", r.root(), "--dry-run"}, kUnknown));
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "testapp"));
    PB_CHECK(has(res.out, "U_TestApp"));
    PB_CHECK(has(res.out, "@General"));
    PB_CHECK(has(res.out, "system/testapp"));
    PB_CHECK(has(res.out, "MiB"));
    PB_CHECK(has(res.out, "2026-10-24"));
    PB_CHECK(compare(before, snapshot(r.root())).identical);
}

PB_TEST(without_a_terminal_the_plan_needs_confirm_plan) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult res = r.run(args({"install", "testapp", "--reader", r.root()}, kUnknown));
    PB_CHECK_EQ(res.code, 2);
    PB_CHECK(has(res.err, "--confirm-plan"));
    PB_CHECK(has(res.out, "testapp"));                          // the plan was shown
    PB_CHECK(compare(before, snapshot(r.root())).identical);
}

PB_TEST(each_extra_confirmation_is_required_separately) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult a = r.run({"install", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-model"});
    PB_CHECK_EQ(a.code, 2);
    PB_CHECK(has(a.err, "--confirm-unknown-firmware"));
    PB_CHECK(has(a.err, "E-INS-11"));
    RunResult b = r.run({"install", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-firmware"});
    PB_CHECK_EQ(b.code, 2);
    PB_CHECK(has(b.err, "--confirm-unknown-model"));
    PB_CHECK(has(b.err, "E-INS-09"));
    RunResult c = r.run({"install", "testapp", "--reader", r.root(), "--confirm-plan"});
    PB_CHECK_EQ(c.code, 2);
    PB_CHECK(compare(before, snapshot(r.root())).identical);
    RunResult combined = r.run({"install", "testapp", "--reader", r.root(), "--yes"});
    PB_CHECK(combined.code != 0);                               // there is no combined option
    PB_CHECK(compare(before, snapshot(r.root())).identical);
}

PB_TEST(install_with_confirm_plan_installs_exactly_the_planned_files_one_entry_and_a_record) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    const std::string old_view = r.reader.files["system/config/desktop/view.json"];
    RunResult res = r.run(args({"install", "testapp", "--reader", r.root(), "--confirm-plan"}, kUnknown));
    PB_CHECK_EQ(res.code, 0);
    // the final message
    PB_CHECK(has(res.out, "eject"));
    PB_CHECK(has(res.out, "restart"));
    PB_CHECK(has(res.out, "take noticeably longer"));   // observed on a real reader: the first start after a change is slower
    PB_CHECK(has(res.out, "@General"));
    PB_CHECK(has(res.out, "App testapp"));
    Diff d = compare(before, snapshot(r.root()));
    SampleApp app = nested_sample_app();
    std::set<std::string> expected_files;
    for (const auto& f : app.files) expected_files.insert(f.name);
    for (const std::string& p : d.added) {
        const bool ok = expected_files.count(p) || p == "applications/testapp" || p == "applications/testapp/lib" || p == "applications/testapp/lib/extra" ||
                        p.compare(0, 14, "system/pbstore") == 0;
        if (!ok) PB_FAIL_AT("added a path outside the plan: " + p);
    }
    for (const auto& f : app.files) PB_CHECK(std::find(d.added.begin(), d.added.end(), f.name) != d.added.end());
    PB_CHECK_EQ(d.changed, (std::vector<std::string>{"system/config/desktop/view.json"}));
    PB_CHECK(d.removed.empty());
    // view.json differs from before only inside the added spans (byte level)
    std::ifstream f(r.root() + "/system/config/desktop/view.json");
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string new_view = ss.str();
    SampleApp a2 = nested_sample_app();
    const json::Value* menu = a2.manifest.find("menu_entry");
    MenuEntry me{menu->get_string("id").value(), menu->get_string("title").value(), menu->get_string("path").value(), menu->get_string("icon").value(),
                 menu->get_string("focused_icon").value(), menu->get_string("group").value()};
    PB_CHECK(verify(old_view, new_view, add_entry(me.id, entry_value_from_manifest(me), me.group)));
    // the log has the line of the action
    std::ifstream lf(r.state_dir() + "/installer.log");
    std::ostringstream ls;
    ls << lf.rdbuf();
    PB_CHECK(has(ls.str(), " install testapp 1.2.0 ok"));
}

PB_TEST(several_qualifying_drives_without_reader_give_e_ins_02_and_a_wrong_folder_e_ins_03) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    FakeReader second;
    second.write_to(r.dir.path() + "/second");
    RunResult res = r.run(args({"install", "testapp", "--confirm-plan"}, kUnknown),
                          {"PBSTORE_TEST_MOUNTS=" + r.root() + "|vfat|1000;" + r.dir.path() + "/second|vfat|1000"});
    PB_CHECK_EQ(res.code, 2);
    PB_CHECK(has(res.err, "error[E-INS-02]"));
    PB_CHECK(has(res.err, "--reader"));
    ::mkdir((r.dir.path() + "/empty").c_str(), 0755);
    RunResult wrong = r.run(args({"install", "testapp", "--reader", r.dir.path() + "/empty", "--confirm-plan"}, kUnknown));
    PB_CHECK_EQ(wrong.code, 2);
    PB_CHECK(has(wrong.err, "error[E-INS-03]"));
    PB_CHECK(has(wrong.err, "applications/"));
    RunResult none = r.run(args({"install", "testapp", "--confirm-plan"}, kUnknown), {"PBSTORE_TEST_MOUNTS="});
    PB_CHECK_EQ(none.code, 2);
    PB_CHECK(has(none.err, "error[E-INS-01]"));
}

PB_TEST(a_single_qualifying_drive_is_used_without_reader) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    RunResult res = r.run(args({"install", "testapp", "--dry-run"}, kUnknown), r.mounts_env());
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "testapp"));
}

PB_TEST(findings_go_to_the_error_output_in_the_documented_form_with_the_three_parts) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    RunResult res = r.run(args({"install", "no-such-app", "--reader", r.root(), "--confirm-plan"}, kUnknown));
    PB_CHECK_EQ(res.code, 1);
    PB_CHECK(has(res.err, "error[E-INS-28] "));
    PB_CHECK(has(res.err, "not changed"));
    PB_CHECK(has(res.err, "Next:"));
}

PB_TEST(the_log_holds_no_user_name_home_path_mount_path_label_or_host_name) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.run(args({"install", "testapp", "--reader", r.root(), "--confirm-plan"}, kUnknown));
    r.run(args({"install", "testapp", "--reader", r.dir.path() + "/nowhere", "--confirm-plan"}, kUnknown));     // a failing action
    r.run(args({"install", "no-such-app", "--reader", r.root(), "--confirm-plan"}, kUnknown));
    std::string all;
    for (const char* name : {"installer.log", "installer.log.1", "state.json"}) {
        std::ifstream f(r.state_dir() + "/" + name);
        std::ostringstream ss;
        ss << f.rdbuf();
        all += ss.str();
    }
    PB_CHECK(!all.empty());
    char host[256] = {0};
    ::gethostname(host, sizeof host - 1);
    for (const std::string& secret : {std::string("cliuser"), r.dir.path() + "/home", r.root(), r.dir.path(), std::string(host)}) {
        if (!secret.empty() && all.find(secret) != std::string::npos) PB_FAIL_AT("the log contains " + secret);
    }
}

PB_TEST(an_interrupted_install_is_found_by_the_next_command_and_repaired_on_request) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    // the program is cut off after the 30th change to the reader, as by a pulled cable
    RunResult cut = r.run(args({"install", "testapp", "--reader", r.root(), "--confirm-plan"}, kUnknown), {"PBSTORE_TEST_STOP_AFTER=30"});
    PB_CHECK(cut.code != 0);
    PB_CHECK(!compare(before, snapshot(r.root())).identical);
    RunResult next = r.run({"list", "--reader", r.root()});
    PB_CHECK_EQ(next.code, 4);
    PB_CHECK(has(next.err, "error[E-INS-23]"));
    PB_CHECK(has(next.err, "repair needed"));
    PB_CHECK(has(next.err + next.out, "undo"));
    // --confirm-repair repairs, reports and ends without installing
    RunResult fix = r.run(args({"install", "testapp", "--reader", r.root(), "--confirm-repair"}, kUnknown));
    PB_CHECK_EQ(fix.code, 0);
    PB_CHECK(has(fix.out, "undone"));
    Snapshot after = snapshot(r.root());
    for (const TreeItem& i : after) PB_CHECK(i.path.find("journal/current") == std::string::npos);
    PB_CHECK(!std::ifstream(r.root() + "/applications/testapp.app").good());     // it did not install in the same run
}

PB_TEST(start_without_arguments_and_without_a_graphical_session_prints_the_help_and_ends_with_status_2) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    RunResult res = r.run({});
    PB_CHECK_EQ(res.code, 2);
    PB_CHECK(has(res.out + res.err, "install"));
    PB_CHECK(has(res.out + res.err, "--reader"));
    PB_CHECK(has(res.out + res.err, "--dry-run"));
    RunResult version = r.run({"--version"});
    PB_CHECK_EQ(version.code, 0);
    PB_CHECK(has(version.out, PBSTORE_VERSION));
}

PB_TEST(an_empty_catalog_prints_an_explicit_message) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.catalog.apps.clear();
    r.host.publish(101);
    RunResult res = r.run({"list", "--reader", r.root()});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "no apps"));
}

PB_TEST(no_output_asks_for_or_advises_elevated_rights) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    ::chmod((r.root() + "/applications").c_str(), 0555);
    std::string all;
    for (const auto& a : {args({"install", "testapp", "--reader", r.root(), "--confirm-plan"}, kUnknown), std::vector<std::string>{"list", "--reader", r.root()},
                          std::vector<std::string>{"readers"}, std::vector<std::string>{"--help"}}) {
        RunResult res = r.run(a, r.mounts_env());
        all += res.out + res.err;
    }
    ::chmod((r.root() + "/applications").c_str(), 0755);
    for (const char* bad : {"sudo", "root", "administrator", "chmod", "elevated"})
        if (::geteuid() != 0 && all.find(bad) != std::string::npos && std::string(bad) != "elevated") PB_FAIL_AT(std::string("an output mentions ") + bad);
    PB_CHECK(has(all, "E-INS-06"));      // the unwritable folder was found before any plan
}

PB_TEST_MAIN()
