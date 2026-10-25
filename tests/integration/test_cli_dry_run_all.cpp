// T104 (spec SC-004): for every action a dry run prints exactly the plan the real run prints, writes nothing to
// the reader (no backup, no record, no staging folder, no system/pbstore/ created) and leaves it byte-identical.
// A dry run does fetch and verify, and may update the remembered serial and the log on the computer.
#include "install_rig.h"
#include "test_support.h"

#include <sys/stat.h>

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

// The text of the plan: everything the dry run printed except its closing line.
std::string plan_text(const std::string& dry_out) {
    const size_t at = dry_out.rfind("This was a dry run");
    return at == std::string::npos ? dry_out : dry_out.substr(0, at);
}

struct DryCase {
    const char* name;
    std::function<FakeReader()> reader;
    std::vector<std::string> command;       // without --reader and the confirmations
    std::function<void(CliRig&)> catalog;
    std::vector<std::string> extra;         // further confirmations the real run needs
};

}  // namespace

PB_TEST(a_dry_run_of_every_action_prints_the_plan_of_the_real_run_and_writes_nothing) {
    const SampleApp v1 = nested_sample_app();
    const SampleApp v2 = nested_sample_app_v2("testapp", "U_TestApp", "1.3.0");
    std::vector<DryCase> cases = {
        {"install", [] { return FakeReader(); }, {"install", "testapp"}, [&](CliRig& r) { r.host.add_app(v1); }, {}},
        {"update", [&] { return with_installed(FakeReader(), v1); }, {"update", "testapp"}, [&](CliRig& r) { r.host.add_app(v2); }, {}},
        {"remove", [&] { return with_installed(FakeReader(), v1); }, {"remove", "testapp"}, [&](CliRig& r) { r.host.add_app(v1); }, {}},
        {"remove with data", [&] { FakeReader f; f.add_data_dir("testapp", {{"a.db", "12"}}); return with_installed(f, v1); },
         {"remove", "testapp", "--delete-data"}, [&](CliRig& r) { r.host.add_app(v1); }, {"--confirm-delete-data-1=system/testapp", "--confirm-delete-data-2=1"}},
        {"adopt", [&] { FakeReader f; f.add_hand_installed(v1); return f; }, {"adopt", "testapp"}, [&](CliRig& r) { r.host.add_app(v1); }, {}},
        {"replace", [&] { FakeReader f; f.add_hand_installed(nested_sample_app("testapp", "1.0.0", "U_TestApp", 2)); return f; }, {"replace", "testapp"},
         [&](CliRig& r) { r.host.add_app(v1); }, {"--confirm-replace"}},
    };
    for (DryCase& c : cases) {
        CliRig r(c.reader());
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        c.catalog(r);
        r.host.publish(100);
        ::mkdir((r.dir.path() + "/home").c_str(), 0755);
        r.write_reader();
        const bool had_area = std::ifstream(r.root() + "/system/pbstore/installed").good();
        Snapshot before = snapshot(r.root());
        // a fixed clock: the plan names the backup file after the time, and the two runs may fall in different seconds
        RunResult dry = r.run(args(args(c.command, {"--reader", r.root(), "--dry-run"}), kUnknown), {"PBSTORE_TEST_NOW=1"});
        if (dry.code != 0) PB_FAIL_AT(std::string(c.name) + ": the dry run ended with status " + std::to_string(dry.code) + "\n" + dry.err);
        Diff d = compare(before, snapshot(r.root()));
        if (!d.identical) PB_FAIL_AT(std::string(c.name) + ": the dry run changed the reader: " + d.describe());
        if (!had_area) PB_CHECK(!std::ifstream(r.root() + "/system/pbstore").good());
        PB_CHECK(has(dry.out, "This was a dry run"));
        // the real run prints that very plan first
        RunResult real = r.run(args(args(args(c.command, {"--reader", r.root(), "--confirm-plan"}), kUnknown), c.extra), {"PBSTORE_TEST_NOW=1"});
        if (real.code != 0) PB_FAIL_AT(std::string(c.name) + ": the real run ended with status " + std::to_string(real.code) + "\n" + real.err);
        const std::string plan = plan_text(dry.out);
        if (real.out.compare(0, plan.size(), plan) != 0) PB_FAIL_AT(std::string(c.name) + ": the real run does not print the plan of the dry run first");
    }
}

PB_TEST(a_dry_run_of_a_local_install_and_of_a_repair_writes_nothing) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    r.write_reader();
    r.dir.write("testapp.zip", build_package(nested_sample_app()));
    Snapshot before = snapshot(r.root());
    RunResult dry = r.run(args({"install", "--file", r.dir.file("testapp.zip"), "--reader", r.root(), "--dry-run"}, kUnknown), {"PBSTORE_TEST_CATALOG_URL=https://localhost:1/catalog.json"});
    PB_CHECK_EQ(dry.code, 0);
    PB_CHECK(compare(before, snapshot(r.root())).identical);
    RunResult real = r.run(args({"install", "--file", r.dir.file("testapp.zip"), "--reader", r.root(), "--confirm-plan"}, kUnknown), {"PBSTORE_TEST_CATALOG_URL=https://localhost:1/catalog.json"});
    PB_CHECK_EQ(real.code, 0);
    const std::string plan = plan_text(dry.out);
    PB_CHECK(real.out.compare(0, plan.size(), plan) == 0);

    // an interrupted operation: the dry run of the repair shows it and changes nothing
    CliRig cut;
    if (!cut.ready()) PB_SKIP("openssl CLI or the test program is not available");
    cut.host.add_app(nested_sample_app());
    cut.host.publish(100);
    ::mkdir((cut.dir.path() + "/home").c_str(), 0755);
    cut.write_reader();
    cut.run(args({"install", "testapp", "--reader", cut.root(), "--confirm-plan"}, kUnknown), {"PBSTORE_TEST_STOP_AFTER=30"});
    Snapshot interrupted = snapshot(cut.root());
    RunResult rd = cut.run({"repair", "--reader", cut.root(), "--dry-run"});
    PB_CHECK_EQ(rd.code, 0);
    PB_CHECK(has(rd.out, "Repair:"));
    PB_CHECK(has(rd.out, "testapp"));
    PB_CHECK(compare(interrupted, snapshot(cut.root())).identical);
    RunResult fix = cut.run({"repair", "--reader", cut.root(), "--confirm-repair"});
    PB_CHECK_EQ(fix.code, 0);
}

PB_TEST_MAIN()
