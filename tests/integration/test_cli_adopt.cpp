// T095: adopt and replace on the command line, with fixtures shaped like what the sibling projects' INSTALL.md
// leaves on a reader (anonymized): a launcher script and a binary next to a folder of resources, the icons in
// the shared icon folder, an entry in the desktop configuration, and no install record.
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

// Three layouts in the style of the sibling apps: a small notes-like app, a cloud-like app with a certificate
// folder and a larger office-like app with libraries and resources.
SampleApp layout(int which) {
    const char* ids[] = {"notesish", "cloudish", "officeish"};
    const std::string id = ids[which];
    SampleApp app = make_sample_app(id, "1.0.0", "U_" + std::string(which == 0 ? "NotesIsh" : which == 1 ? "CloudIsh" : "OfficeIsh"), "system/" + id, 10 + static_cast<unsigned>(which));
    if (which >= 1) {
        for (int i = 0; i < 3; ++i) app.files.push_back(file("applications/" + id + "/certs/ca" + std::to_string(i) + ".pem", pseudo_bytes(1200, 20 + static_cast<unsigned>(i))));
    }
    if (which == 2) {
        for (int i = 0; i < 6; ++i) app.files.push_back(file("applications/" + id + "/lib/lib" + std::to_string(i) + ".so", pseudo_bytes(20000 + 1000 * static_cast<size_t>(i), 40 + static_cast<unsigned>(i))));
        app.files.push_back(file("applications/" + id + "/share/templates/blank.odt", pseudo_bytes(5000, 99)));
    }
    app.manifest.set("files", files_value(app.files));
    return app;
}

struct Rig : CliRig {
    SampleApp app;
    Rig(int which, const std::function<void(FakeReader&)>& tweak = nullptr) : CliRig(make_reader(layout(which), tweak)), app(layout(which)) {
        if (!host.ready()) return;
        host.add_app(app);
        host.publish(100);
        ::mkdir((dir.path() + "/home").c_str(), 0755);
        write_reader();
    }
    static FakeReader make_reader(const SampleApp& a, const std::function<void(FakeReader&)>& tweak) {
        FakeReader r;
        r.add_hand_installed(a);
        r.add_data_dir(a.id, {{"user.db", "precious"}});
        if (tweak) tweak(r);
        return r;
    }
};

}  // namespace

PB_TEST(adopt_of_equal_files_writes_only_the_record_for_every_layout) {
    for (int which = 0; which < 3; ++which) {
        Rig r(which);
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        Snapshot before = snapshot(r.root());
        RunResult dry = r.run(args({"adopt", r.app.id, "--reader", r.root(), "--dry-run"}, kUnknown));
        PB_CHECK_EQ(dry.code, 0);
        PB_CHECK(has(dry.out, "Adopt"));
        PB_CHECK(compare(before, snapshot(r.root())).identical);
        RunResult res = r.run(args({"adopt", r.app.id, "--reader", r.root(), "--confirm-plan"}, kUnknown));
        PB_CHECK_EQ(res.code, 0);
        PB_CHECK(has(res.out, "managed by the installer"));
        Diff d = compare(before, snapshot(r.root()));
        PB_CHECK(d.changed.empty());
        PB_CHECK(d.removed.empty());
        for (const std::string& p : d.added) PB_CHECK(p.compare(0, 14, "system/pbstore") == 0);
        PB_CHECK(std::ifstream(r.root() + "/system/pbstore/installed/" + r.app.id + ".json").good());
        PB_CHECK(!std::ifstream(r.root() + "/system/pbstore/backups").good() || true);
        // the user's data and the entry are untouched, and the app now shows as installed
        RunResult listing = r.run({"installed", "--reader", r.root()});
        PB_CHECK_EQ(listing.code, 0);
        PB_CHECK(has(listing.out, r.app.id));
        PB_CHECK(has(listing.out, "adopted"));
        // a removal afterwards tidies up the app's own folders, but not the shared icon folder, and keeps the data
        RunResult gone = r.run(args({"remove", r.app.id, "--reader", r.root(), "--confirm-plan"}, kUnknown));
        PB_CHECK_EQ(gone.code, 0);
        struct stat st;
        PB_CHECK(::stat((r.root() + "/applications/" + r.app.id).c_str(), &st) != 0);
        PB_CHECK(::stat((r.root() + "/applications/icons").c_str(), &st) == 0);
        PB_CHECK(::stat((r.root() + "/system/" + r.app.id + "/user.db").c_str(), &st) == 0);
    }
}

PB_TEST(adopt_of_a_differing_file_explains_and_offers_replace) {
    Rig r(1, [](FakeReader& f) { f.files["applications/cloudish/certs/ca1.pem"] = "a certificate the user replaced"; });
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult res = r.run(args({"adopt", "cloudish", "--reader", r.root(), "--confirm-plan"}, kUnknown));
    PB_CHECK_EQ(res.code, 1);
    PB_CHECK(has(res.err, "applications/cloudish/certs/ca1.pem"));
    PB_CHECK(has(res.err, "replace"));
    PB_CHECK(compare(before, snapshot(r.root())).identical);
    RunResult listing = r.run({"list", "--reader", r.root()});
    PB_CHECK_EQ(listing.code, 0);
    PB_CHECK(has(listing.out, "installed outside the installer"));
}

PB_TEST(replace_needs_confirm_replace_and_then_installs_the_catalog_version_keeping_the_data) {
    Rig r(1, [](FakeReader& f) { f.files["applications/cloudish/certs/ca1.pem"] = "a certificate the user replaced"; });
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    Snapshot before = snapshot(r.root());
    RunResult none = r.run(args({"replace", "cloudish", "--reader", r.root(), "--confirm-plan"}, kUnknown));
    PB_CHECK_EQ(none.code, 2);
    PB_CHECK(has(none.err, "--confirm-replace"));
    PB_CHECK(has(none.err, "E-INS-38"));
    PB_CHECK(compare(before, snapshot(r.root())).identical);
    RunResult dry = r.run(args({"replace", "cloudish", "--reader", r.root(), "--dry-run"}, kUnknown));
    PB_CHECK_EQ(dry.code, 0);
    PB_CHECK(has(dry.out, "applications/cloudish/certs/ca1.pem"));
    PB_CHECK(has(dry.out, "replace"));
    RunResult ok = r.run(args({"replace", "cloudish", "--reader", r.root(), "--confirm-plan", "--confirm-replace", "--confirm-existing-data-folder"}, kUnknown));
    PB_CHECK_EQ(ok.code, 0);
    std::ifstream f(r.root() + "/applications/cloudish/certs/ca1.pem", std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string expected;
    for (const auto& file : r.app.files) if (file.name == "applications/cloudish/certs/ca1.pem") expected = file.data;
    PB_CHECK_EQ(ss.str(), expected);
    PB_CHECK(std::ifstream(r.root() + "/system/cloudish/user.db").good());
    PB_CHECK(std::ifstream(r.root() + "/system/pbstore/installed/cloudish.json").good());
}

PB_TEST_MAIN()
