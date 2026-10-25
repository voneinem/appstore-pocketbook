// T079: remove on the command line; the data folder stays unless it is asked for twice, differently.
#include "install_rig.h"
#include "test_support.h"

#include <sys/stat.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

struct Rig : CliRig {
    explicit Rig(bool with_data = true) : CliRig(make_reader(with_data)) {
        if (!host.ready()) return;
        host.add_app(nested_sample_app());
        host.publish(100);
        ::mkdir((dir.path() + "/home").c_str(), 0755);
        write_reader();
    }
    static FakeReader make_reader(bool with_data) {
        FakeReader r;
        if (with_data) r.add_data_dir("testapp", {{"notes.db", "12345"}, {"sub/b.db", "123"}});
        return with_installed(r, nested_sample_app());
    }
};

}  // namespace

PB_TEST(remove_takes_the_recorded_files_and_the_entry_and_keeps_the_data_folder) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult res = r.run({"remove", "testapp", "--reader", r.root(), "--confirm-plan"});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "removed"));
    PB_CHECK(has(res.out, "system/testapp"));
    PB_CHECK(has(res.out, "kept"));
    PB_CHECK(has(res.out, "eject"));
    PB_CHECK(has(res.out, "restart"));
    PB_CHECK(has(res.out, "take noticeably longer"));   // observed on a real reader: the first start after a change is slower
    for (const auto& f : nested_sample_app().files) PB_CHECK(!std::ifstream(r.root() + "/" + f.name).good());
    PB_CHECK(std::ifstream(r.root() + "/system/testapp/notes.db").good());
    Diff d = compare(before, snapshot(r.root()));
    for (const std::string& p : d.added) PB_CHECK(p.compare(0, 14, "system/pbstore") == 0);
    PB_CHECK(std::find(d.changed.begin(), d.changed.end(), "system/config/desktop/view.json") != d.changed.end());
}

PB_TEST(delete_data_with_both_confirmations_deletes_the_folder) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    RunResult res = r.run({"remove", "testapp", "--reader", r.root(), "--confirm-plan", "--delete-data", "--confirm-delete-data-1=system/testapp", "--confirm-delete-data-2=2"});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "data folder"));
    PB_CHECK(has(res.out, "deleted"));
    PB_CHECK(!std::ifstream(r.root() + "/system/testapp/notes.db").good());
    struct stat st;
    PB_CHECK(::stat((r.root() + "/system/testapp").c_str(), &st) != 0);
}

PB_TEST(a_missing_or_wrong_data_confirmation_keeps_the_folder_and_names_the_option) {
    {
        Rig r;
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        RunResult res = r.run({"remove", "testapp", "--reader", r.root(), "--confirm-plan", "--delete-data"});
        PB_CHECK_EQ(res.code, 2);
        PB_CHECK(has(res.err, "--confirm-delete-data-1"));
        PB_CHECK(has(res.out, "removed"));                       // the removal itself was done
        PB_CHECK(std::ifstream(r.root() + "/system/testapp/notes.db").good());
    }
    {
        Rig r;
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        RunResult res = r.run({"remove", "testapp", "--reader", r.root(), "--confirm-plan", "--delete-data", "--confirm-delete-data-1=system/testapp"});
        PB_CHECK_EQ(res.code, 2);
        PB_CHECK(has(res.err, "--confirm-delete-data-2"));
        PB_CHECK(std::ifstream(r.root() + "/system/testapp/notes.db").good());
    }
    {
        Rig r;
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        RunResult res = r.run({"remove", "testapp", "--reader", r.root(), "--confirm-plan", "--delete-data", "--confirm-delete-data-1=system/testapp", "--confirm-delete-data-2=7"});
        PB_CHECK_EQ(res.code, 2);
        PB_CHECK(has(res.err, "does not match"));
        PB_CHECK(std::ifstream(r.root() + "/system/testapp/notes.db").good());
    }
}

PB_TEST(without_confirm_plan_the_removal_needs_it_and_changes_nothing) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult res = r.run({"remove", "testapp", "--reader", r.root()});
    PB_CHECK_EQ(res.code, 2);
    PB_CHECK(has(res.err, "--confirm-plan"));
    PB_CHECK(compare(before, snapshot(r.root())).identical);
}

PB_TEST(remove_of_an_app_outside_the_installer_gives_e_ins_31_and_a_dry_run_changes_nothing) {
    FakeReader hand;
    hand.add_hand_installed(make_sample_app("testapp", "1.2.0", "U_TestApp", "system/testapp"));
    CliRig r(hand);
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.add_app(nested_sample_app());
    r.host.publish(100);
    r.write_reader();
    Snapshot before = snapshot(r.root());
    RunResult res = r.run({"remove", "testapp", "--reader", r.root(), "--confirm-plan"});
    PB_CHECK_EQ(res.code, 1);
    PB_CHECK(has(res.err, "error[E-INS-31]"));
    PB_CHECK(has(res.err, "adopt"));
    PB_CHECK(compare(before, snapshot(r.root())).identical);
    Rig installed;
    if (!installed.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot b2 = snapshot(installed.root());
    RunResult dry = installed.run({"remove", "testapp", "--reader", installed.root(), "--dry-run", "--delete-data"});
    PB_CHECK_EQ(dry.code, 0);
    PB_CHECK(compare(b2, snapshot(installed.root())).identical);
    PB_CHECK(has(dry.out, "deleted after two further confirmations"));
}

PB_TEST(remove_of_an_app_that_is_not_installed_gives_e_ins_30) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.publish(100);
    r.write_reader();
    RunResult res = r.run({"remove", "testapp", "--reader", r.root(), "--confirm-plan"});
    PB_CHECK_EQ(res.code, 1);
    PB_CHECK(has(res.err, "error[E-INS-30]"));
}

PB_TEST_MAIN()
