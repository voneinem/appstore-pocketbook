// T106 (spec SC-010): the log holds no user name, home path, mount path, volume label, host name, device
// serial, local file path or credential, whatever was run; it stays on the computer and nothing is written to
// the reader except under system/pbstore/, the app's files and the one desktop configuration entry.
#include "install_rig.h"
#include "test_support.h"

#include <sys/stat.h>
#include <unistd.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
const std::vector<std::string> kUnknown = {"--confirm-unknown-model", "--confirm-unknown-firmware"};
std::vector<std::string> args_with(std::vector<std::string> a, const std::vector<std::string>& more) {
    a.insert(a.end(), more.begin(), more.end());
    return a;
}
}  // namespace

PB_TEST(nothing_personal_reaches_the_log_whatever_was_run) {
    FakeReader f;
    f.put_file("system/config/device.cfg", "serial=SN-9X7Q-2026-ABCDEF\n");        // a device serial on the storage
    CliRig r(f);
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.add_app(nested_sample_app());
    r.host.add_app(nested_sample_app("secondapp", "1.0.0", "U_SecondApp"));
    r.host.publish(100);
    const std::string home = r.dir.path() + "/home/ernestine.musterfrau";
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    ::mkdir(home.c_str(), 0755);
    r.write_reader();
    const std::string package = home + "/Downloads-privat/testapp-secret-name-1.2.0.zip";
    ::mkdir((home + "/Downloads-privat").c_str(), 0755);
    r.dir.write("home/ernestine.musterfrau/Downloads-privat/testapp-secret-name-1.2.0.zip", build_package(nested_sample_app("localonly", "1.0.0", "U_LocalOnly")));

    const std::vector<std::string> env = {"HOME=" + home, "USER=ernestine.musterfrau", "LOGNAME=ernestine.musterfrau", "PBSTORE_TEST_MOUNTS=" + r.root() + "|vfat|1000",
                                          "HTTPS_PROXY=http://proxyuser:proxysecret@proxy.invalid:3128", "NO_PROXY=localhost,127.0.0.1", "DEVICE_SERIAL=SN-9X7Q-2026-ABCDEF"};
    // run everything, including failures and a local file
    auto go = [&](const std::vector<std::string>& a, const std::vector<std::string>& more = {}) {
        std::vector<std::string> e = env;
        e.insert(e.end(), more.begin(), more.end());
        return r.run(a, e);
    };
    go({"readers"});
    go({"list"});
    go({"installed"});
    go(args_with({"install", "testapp", "--dry-run"}, kUnknown));
    go(args_with({"install", "testapp", "--confirm-plan"}, kUnknown));
    go(args_with({"install", "testapp", "--confirm-plan"}, kUnknown));                          // again: nothing to do
    go(args_with({"install", "no-such-app", "--confirm-plan"}, kUnknown));                      // fails
    go(args_with({"install", "secondapp", "--reader", r.dir.path() + "/not-a-reader", "--confirm-plan"}, kUnknown));   // fails
    go(args_with({"install", "--file", package, "--confirm-plan"}, kUnknown), {"PBSTORE_TEST_CATALOG_URL=https://localhost:1/catalog.json"});
    go(args_with({"install", "--file", home + "/nothing-here.zip", "--confirm-plan"}, kUnknown));
    go({"remove", "testapp", "--confirm-plan"});
    go({"repair"});
    go(args_with({"install", "secondapp", "--confirm-plan"}, kUnknown), {"PBSTORE_TEST_STOP_AFTER=20"});
    go({"list"});                                                                               // the interrupted operation
    go({"repair", "--confirm-repair"});
    go({"list"}, {"HTTPS_PROXY=http://proxyuser:proxysecret@127.0.0.1:1", "NO_PROXY="});         // the proxy cannot be reached

    std::string log;
    for (const char* name : {"installer.log", "installer.log.1", "installer.log.2", "state.json"}) log += slurp(r.state_dir() + "/" + name);
    PB_CHECK(!log.empty());
    char host[256] = {0};
    ::gethostname(host, sizeof host - 1);
    std::vector<std::string> secrets = {"ernestine", "musterfrau", home, "Downloads-privat", "testapp-secret-name", package, r.root(), "SN-9X7Q", "proxyuser", "proxysecret", "proxy.invalid",
                                        r.dir.path()};
    if (std::string(host).size() > 2) secrets.push_back(host);
    for (const std::string& s : secrets)
        if (log.find(s) != std::string::npos) PB_FAIL_AT("the log contains \"" + s + "\"");
    // the reader's label is the last part of the mount path
    PB_CHECK(log.find("/reader") == std::string::npos);
    // every line has the form <time> <action> <app> <version> <result> ...
    std::istringstream in(log);
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '{') continue;
        PB_CHECK(line.size() > 20 && line[4] == '-' && line[10] == 'T' && line[19] == 'Z');
        ++lines;
    }
    PB_CHECK(lines >= 8);
}

PB_TEST(the_log_stays_on_the_computer_and_nothing_personal_is_written_to_the_reader) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.add_app(nested_sample_app());
    r.host.publish(100);
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    r.write_reader();
    Snapshot before = snapshot(r.root());
    RunResult res = r.run(args_with({"install", "testapp", "--reader", r.root(), "--confirm-plan"}, kUnknown), {"USER=ernestine.musterfrau"});
    PB_CHECK_EQ(res.code, 0);
    Diff d = compare(before, snapshot(r.root()));
    SampleApp app = nested_sample_app();
    std::set<std::string> allowed;
    for (const auto& f : app.files) allowed.insert(f.name);
    for (const std::string& p : d.added) {
        const bool ok = allowed.count(p) || p.compare(0, 13, "applications/") == 0 || p.compare(0, 14, "system/pbstore") == 0;
        if (!ok) PB_FAIL_AT("something was added outside the allowed places: " + p);
    }
    for (const std::string& p : d.changed) PB_CHECK(p == "system/config/desktop/view.json");
    // no file on the reader contains the user name or the computer's paths
    for (const TreeItem& i : snapshot(r.root())) {
        if (i.kind != 'f') continue;
        const std::string content = slurp(r.root() + "/" + i.path);
        PB_CHECK(content.find("ernestine") == std::string::npos);
        PB_CHECK(content.find(r.dir.path()) == std::string::npos);
    }
    PB_CHECK(!std::ifstream(r.root() + "/installer.log").good());
}

PB_TEST_MAIN()
