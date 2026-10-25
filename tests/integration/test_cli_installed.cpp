// T105: `installed` works offline and shows everything on the reader; only the commands that need the network
// make a connection.
#include "install_rig.h"
#include "test_support.h"

#include <sys/stat.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
const std::vector<std::string> kUnknown = {"--confirm-unknown-model", "--confirm-unknown-firmware"};
}  // namespace

PB_TEST(installed_lists_apps_with_record_apps_outside_the_installer_unknown_apps_and_changed_files) {
    FakeReader f;
    f = with_installed(f, nested_sample_app("recorded", "1.0.0", "U_Recorded"));
    f.add_hand_installed(make_sample_app("byhand", "1.0.0", "U_ByHand", "system/byhand"));
    f.files["applications/recorded.bin"] = "edited";
    CliRig r(f);
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.add_app(nested_sample_app("byhand", "1.0.0", "U_ByHand"));
    r.host.publish(100);
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    r.write_reader();
    RunResult res = r.run({"installed", "--reader", r.root()});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "recorded"));
    PB_CHECK(has(res.out, "installed\t"));
    PB_CHECK(has(res.out, "installed 1.0.0"));
    PB_CHECK(has(res.out, "source catalog"));
    PB_CHECK(has(res.out, "files changed since the install"));
    PB_CHECK(has(res.out, "U_AlphaApp"));                       // an entry that is neither recorded nor known
    PB_CHECK(has(res.out, "not in the catalog"));
    PB_CHECK(!has(res.out, "Dictionary"));                      // the vendor's own entries are not listed
    // offline: the server saw nothing
    PB_CHECK(r.host.request_paths().empty());
}

PB_TEST(the_commands_that_need_no_network_make_no_connection) {
    FakeReader f = with_installed(FakeReader(), nested_sample_app());
    CliRig r(f);
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.add_app(nested_sample_app());
    r.host.publish(100);
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    r.write_reader();
    for (const std::vector<std::string>& cmd : {std::vector<std::string>{"readers"}, std::vector<std::string>{"installed", "--reader", r.root()},
                                                 std::vector<std::string>{"repair", "--reader", r.root()}, std::vector<std::string>{"--version"},
                                                 std::vector<std::string>{"remove", "testapp", "--reader", r.root(), "--confirm-plan"}}) {
        r.run(cmd, {"PBSTORE_TEST_MOUNTS=" + r.root() + "|vfat|1000"});
        PB_CHECK(r.host.request_paths().empty());
    }
    // and the commands marked as network commands do
    RunResult list = r.run({"list", "--reader", r.root()});
    PB_CHECK(!r.host.request_paths().empty());
}

PB_TEST(installed_on_a_reader_with_no_apps_says_so) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    // a desktop configuration without any U_ entry
    r.reader.entries.erase(std::remove_if(r.reader.entries.begin(), r.reader.entries.end(), [](const FakeReader::Entry& e) { return e.id.compare(0, 2, "U_") == 0; }), r.reader.entries.end());
    for (auto& g : r.reader.groups) g.apps.erase(std::remove_if(g.apps.begin(), g.apps.end(), [](const std::string& id) { return id.compare(0, 2, "U_") == 0; }), g.apps.end());
    r.reader.sync_view();
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    r.write_reader();
    RunResult res = r.run({"installed", "--reader", r.root()});
    PB_CHECK_EQ(res.code, 0);
    PB_CHECK(has(res.out, "No apps were found"));
}

PB_TEST(installed_shows_an_interrupted_operation_and_ends_with_status_4) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.add_app(nested_sample_app());
    r.host.publish(100);
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    r.write_reader();
    r.run({"install", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"}, {"PBSTORE_TEST_STOP_AFTER=30"});
    RunResult res = r.run({"installed", "--reader", r.root()});
    PB_CHECK_EQ(res.code, 4);
    PB_CHECK(has(res.out, "testapp"));
    PB_CHECK(has(res.out, "interrupted"));
    PB_CHECK(has(res.err, "error[E-INS-23]"));
}

PB_TEST_MAIN()
