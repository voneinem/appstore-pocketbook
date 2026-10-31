// T128 (spec story 7, FR-041): PB Store is installed, updated and removed by the installer like any other app, with no special case. The
// fixture store package (id pbstore, the real launcher as pbstore.app, a small program as pbstore.bin, no data folder) goes through
// pbstore-install-test: the record is true after each step (it lists applications/pbstore.bin, never a .new or .old name); after the real
// launcher ran against the tree (it works on a private copy in system/pbstore/app/run/) the installer asks for no confirmation of changed
// files; the removal deletes the recorded files and leaves system/pbstore/app/ with everything in it, and says so (W-INS-04).
#include <sys/stat.h>

#include <fstream>
#include <sstream>

#include "install_rig.h"
#include "test_support.h"

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

// The store package as the release builds it: the real launcher and a program that reaches its first screen at once.
SampleApp store_package(const std::string& version, unsigned seed) {
    SampleApp app = make_sample_app("pbstore", version, "U_PbStore", "", seed);
    for (ZEntry& f : app.files) {
        if (f.name == "applications/pbstore.app") f.data = slurp(std::string(PBSTORE_SOURCE_DIR) + "/platform/pocketbook/pbstore.app");
        if (f.name == "applications/pbstore.bin") f.data = "#!/bin/sh\n# PB Store " + version + "\nrm -f \"$(dirname \"$0\")/trying\"\nexit 0\n";
    }
    app.manifest.set("files", files_value(app.files));
    return app;
}

const std::vector<std::string> kConfirm = {"--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"};
std::vector<std::string> with(std::vector<std::string> a, const std::vector<std::string>& more) {
    a.insert(a.end(), more.begin(), more.end());
    return a;
}

}  // namespace

PB_TEST(the_store_package_is_installed_updated_and_removed_by_the_installer_with_no_special_case) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    const SampleApp v1 = store_package("0.1.0", 11), v2 = store_package("0.2.0", 22);
    r.host.add_app(v1);
    r.host.publish(100);
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    r.write_reader();

    // install
    RunResult in = r.run(with({"install", "pbstore", "--reader", r.root()}, kConfirm));
    PB_CHECK_EQ(in.code, 0);
    for (const ZEntry& f : v1.files) PB_CHECK_EQ(slurp(r.root() + "/" + f.name), f.data);
    auto record_names = [&](const std::string& expected_version) {
        const std::string rec = slurp(r.root() + "/system/pbstore/installed/pbstore.json");
        PB_CHECK(has(rec, "\"applications/pbstore.bin\""));
        PB_CHECK(has(rec, expected_version));
        PB_CHECK(!has(rec, ".new") && !has(rec, ".old") && !has(rec, ".tmp") && !has(rec, ".failed"));
        return rec;
    };
    const std::string rec1 = record_names("0.1.0");

    // the real launcher starts the store: it works on a private copy, so the files of the package stay as installed
    const std::string launcher = r.dir.path() + "/launch.sh";
    {
        std::string script = slurp(r.root() + "/applications/pbstore.app");
        const std::string from = "STORAGE=/mnt/ext1";
        const size_t at = script.find("\n" + from);
        PB_CHECK(at != std::string::npos);
        script.replace(at + 1, from.size(), "STORAGE=" + r.root());
        std::ofstream(launcher) << script;
    }
    RunResult started = run_program("/bin/sh", {launcher});
    PB_CHECK_EQ(started.code, 0);
    struct stat st;
    PB_CHECK_EQ(::stat((r.root() + "/system/pbstore/app/run/pbstore.bin").c_str(), &st), 0);   // the private copy
    for (const ZEntry& f : v1.files) PB_CHECK_EQ(slurp(r.root() + "/" + f.name), f.data);      // the installed files are untouched
    PB_CHECK_EQ(slurp(r.root() + "/system/pbstore/installed/pbstore.json"), rec1);

    // update: no confirmation of changed files, the record says the new version
    r.host.add_app(v2);
    r.host.publish(200);
    RunResult up = r.run(with({"update", "pbstore", "--reader", r.root()}, kConfirm));
    if (up.code != 0) PB_FAIL_AT("the update asked for more than the plan: " + up.err);
    PB_CHECK(!has(up.err, "E-INS-32"));
    for (const ZEntry& f : v2.files) PB_CHECK_EQ(slurp(r.root() + "/" + f.name), f.data);
    record_names("0.2.0");

    // the next start runs the new program from a new private copy; the installer still sees nothing changed
    RunResult again = run_program("/bin/sh", {launcher});
    PB_CHECK_EQ(again.code, 0);
    PB_CHECK(has(slurp(r.root() + "/system/pbstore/app/run/pbstore.bin"), "PB Store 0.2.0"));
    RunResult installed = r.run({"installed", "--reader", r.root()});
    PB_CHECK_EQ(installed.code, 0);
    PB_CHECK(!has(installed.out + installed.err, "changed"));

    // the store's own cache and log belong to the reader, not to the package
    { std::ofstream f(r.root() + "/system/pbstore/app/pbstore.log"); f << "a log line\n"; }
    { std::ofstream f(r.root() + "/system/pbstore/app/catalog.json"); f << "{}"; }

    // remove: the recorded files go, system/pbstore/app/ stays with everything in it, and the message says so
    RunResult gone = r.run({"remove", "pbstore", "--reader", r.root(), "--confirm-plan"});
    PB_CHECK_EQ(gone.code, 0);
    PB_CHECK(has(gone.out + gone.err, "system/pbstore/app/"));
    PB_CHECK(has(gone.out + gone.err, "were kept"));
    for (const ZEntry& f : v2.files) PB_CHECK_EQ(::stat((r.root() + "/" + f.name).c_str(), &st), -1);
    PB_CHECK_EQ(::stat((r.root() + "/system/pbstore/installed/pbstore.json").c_str(), &st), -1);
    PB_CHECK_EQ(slurp(r.root() + "/system/pbstore/app/pbstore.log"), std::string("a log line\n"));
    PB_CHECK_EQ(slurp(r.root() + "/system/pbstore/app/catalog.json"), std::string("{}"));
    PB_CHECK_EQ(::stat((r.root() + "/system/pbstore/app/run/pbstore.bin").c_str(), &st), 0);   // the launcher's own copy stays too
}

PB_TEST_MAIN()
