// T088: update on the command line.
#include "install_rig.h"
#include "test_support.h"

#include <sys/stat.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct Rig : CliRig {
    Rig() : CliRig(make_reader()) {
        if (!host.ready()) return;
        host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
        host.publish(100);
        ::mkdir((dir.path() + "/home").c_str(), 0755);
        write_reader();
    }
    static FakeReader make_reader() {
        FakeReader r;
        r.add_data_dir("testapp", {{"notes.db", "precious"}});
        return with_installed(r, nested_sample_app());
    }
};

}  // namespace

PB_TEST(list_marks_the_update_with_both_versions) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    RunResult res = r.run({"list", "--reader", r.root()});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "update available"));
    PB_CHECK(has(res.out, "1.2.0"));
    PB_CHECK(has(res.out, "1.3.0"));
}

PB_TEST(update_dry_run_shows_the_plan_and_changes_nothing) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult res = r.run({"update", "testapp", "--reader", r.root(), "--dry-run", "--confirm-unknown-model", "--confirm-unknown-firmware"});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "1.2.0 -> 1.3.0"));
    PB_CHECK(has(res.out, "replace"));
    PB_CHECK(has(res.out, "remove"));
    PB_CHECK(has(res.out, "applications/testapp/new.bin"));
    PB_CHECK(compare(before, snapshot(r.root())).identical);
}

PB_TEST(update_with_confirm_plan_installs_the_new_version_and_keeps_the_data_folder) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    RunResult res = r.run({"update", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "1.3.0"));
    PB_CHECK(has(res.out, "eject"));
    PB_CHECK(has(res.out, "data folder was kept"));
    for (const auto& f : nested_sample_app_v2("testapp", "U_TestApp", "1.3.0").files) PB_CHECK_EQ(slurp(r.root() + "/" + f.name), f.data);
    PB_CHECK_EQ(slurp(r.root() + "/system/testapp/notes.db"), std::string("precious"));
    RunResult again = r.run({"update", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"});
    PB_CHECK_EQ(again.code, 0);
    PB_CHECK(has(again.out, "Nothing to do"));
}

PB_TEST(edited_files_need_their_own_confirmation) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    {
        std::ofstream f(r.root() + "/applications/testapp.bin", std::ios::trunc);
        f << "edited by the user";
    }
    Snapshot before = snapshot(r.root());
    RunResult res = r.run({"update", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"});
    PB_CHECK_EQ(res.code, 2);
    PB_CHECK(has(res.err, "--confirm-edited-files"));
    PB_CHECK(has(res.err, "E-INS-32"));
    PB_CHECK(compare(before, snapshot(r.root())).identical);
    RunResult ok = r.run({"update", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-edited-files", "--confirm-unknown-model", "--confirm-unknown-firmware"});
    PB_CHECK_EQ(ok.code, 0);
}

PB_TEST(a_cut_off_update_is_found_by_the_next_command_and_confirm_repair_only_repairs) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult cut = r.run({"update", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"}, {"PBSTORE_TEST_STOP_AFTER=25"});
    PB_CHECK(cut.code != 0);
    RunResult next = r.run({"list", "--reader", r.root()});
    PB_CHECK_EQ(next.code, 4);
    PB_CHECK(has(next.err, "error[E-INS-23]"));
    RunResult fix = r.run({"update", "testapp", "--reader", r.root(), "--confirm-repair", "--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"});
    PB_CHECK_EQ(fix.code, 0);
    PB_CHECK(has(fix.out, "undone") || has(fix.out, "finished"));
    PB_CHECK(!has(fix.out, "Done. "));                    // it repaired and ended; it did not update in the same run
    // the reader holds one complete version: the old one or the new one
    RunResult list = r.run({"list", "--reader", r.root()});
    PB_CHECK_EQ(list.code, 0);
}

PB_TEST_MAIN()
