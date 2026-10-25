// T101: install --file on the command line, with no network.
#include "install_rig.h"
#include "package_fixtures.h"
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
const char* kMark = "not from the catalog, not signed by the maintainer";

struct Rig : CliRig {
    Rig() {
        if (!host.ready()) return;
        ::mkdir((dir.path() + "/home").c_str(), 0755);
        write_reader();
        dir.write("testapp-1.2.0.zip", build_package(nested_sample_app()));
    }
    std::string zip() const { return dir.file("testapp-1.2.0.zip"); }
    // the catalog address points nowhere: the network is off
    std::vector<std::string> offline() const { return {"PBSTORE_TEST_CATALOG_URL=https://localhost:1/catalog.json"}; }
};

}  // namespace

PB_TEST(install_file_works_with_the_network_off_and_every_output_carries_the_marking) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult dry = r.run(args({"install", "--file", r.zip(), "--reader", r.root(), "--dry-run"}, kUnknown), r.offline());
    PB_CHECK_EQ(dry.code, 0);
    // the marking is the first line of the plan, before any question
    PB_CHECK(dry.out.compare(0, 6, "NOTE: ") == 0);
    PB_CHECK(has(dry.out.substr(0, dry.out.find('\n')), kMark));
    PB_CHECK(has(dry.err, "offline"));
    PB_CHECK(compare(before, snapshot(r.root())).identical);
    RunResult res = r.run(args({"install", "--file", r.zip(), "--reader", r.root(), "--confirm-plan"}, kUnknown), r.offline());
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, kMark));
    PB_CHECK(has(res.out, "eject"));
    for (const auto& f : nested_sample_app().files) PB_CHECK(std::ifstream(r.root() + "/" + f.name).good());
    RunResult installed = r.run({"installed", "--reader", r.root()}, r.offline());
    PB_CHECK_EQ(installed.code, 0);
    PB_CHECK(has(installed.out, "source local"));
    PB_CHECK(has(installed.out, kMark));
    // the log has the line; the serial memory was not touched (the catalog was never reached)
    PB_CHECK(!std::ifstream(r.state_dir() + "/state.json").good());
}

PB_TEST(a_rule_breaking_zip_is_refused_with_its_id_and_nothing_is_written) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    int refused = 0;
    for (const Fixture& f : invalid_package_fixtures()) {
        r.dir.write("bad.zip", f.zip);
        RunResult res = r.run(args({"install", "--file", r.dir.file("bad.zip"), "--reader", r.root(), "--confirm-plan"}, kUnknown), r.offline());
        PB_CHECK_EQ(res.code, 1);
        if (!has(res.err, "error[" + f.rule + "]")) PB_FAIL_AT(f.rule + "/" + f.what + ": the rule id is not in the output");
        ++refused;
    }
    PB_CHECK(refused > 30);
    PB_CHECK(compare(before, snapshot(r.root())).identical);
}

PB_TEST(a_file_that_is_not_usable_gives_e_ins_27_with_status_2) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    RunResult missing = r.run(args({"install", "--file", r.dir.file("nothing.zip"), "--reader", r.root(), "--confirm-plan"}, kUnknown), r.offline());
    PB_CHECK_EQ(missing.code, 2);
    PB_CHECK(has(missing.err, "error[E-INS-27]"));
}

PB_TEST(a_local_app_that_is_in_the_catalog_shows_update_available_only_for_a_newer_offered_version) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    RunResult res = r.run(args({"install", "--file", r.zip(), "--reader", r.root(), "--confirm-plan"}, kUnknown), r.offline());
    PB_CHECK_EQ(res.code, 0);
    r.host.add_app(nested_sample_app("testapp", "1.2.0"));      // the catalog offers the same version: no update
    r.host.publish(100);
    RunResult same = r.run({"list", "--reader", r.root()});
    PB_CHECK_EQ(same.code, 0);
    PB_CHECK(!has(same.out, "update available"));
    PB_CHECK(has(same.out, kMark));
    r.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    r.host.publish(101);
    RunResult newer = r.run({"list", "--reader", r.root()});
    PB_CHECK(has(newer.out, "update available"));
    RunResult update = r.run(args({"update", "testapp", "--reader", r.root(), "--confirm-plan"}, kUnknown));
    PB_CHECK_EQ(update.code, 0);
    PB_CHECK(has(update.out, "1.3.0"));
}

PB_TEST_MAIN()
