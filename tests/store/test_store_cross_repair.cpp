// T082, T103 (spec story 6, FR-030): the store and the installer repair each other's interrupted operations, and the result is the same.
// Installer -> store: pbstore-install-test (with its fault hook PBSTORE_TEST_STOP_AFTER) leaves an interrupted install on a temporary reader
// tree; the tree is copied; the store repairs one copy through StoreService, the installer's own `repair` the other; the trees are equal.
// Store -> installer: the same the other way round, with the store's install cut by a lost power (ThrowingFs).
#include <filesystem>

#include "install_rig.h"
#include "store_rig.h"
#include "test_support.h"
#include "throwing_fs.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;
namespace fs = std::filesystem;

namespace {

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

// The reader without what only one of the programs writes: the store's state folder, the lock file and the timestamped backups.
Snapshot comparable(const Snapshot& s) {
    Snapshot out;
    for (const auto& i : s) {
        if (starts_with(i.path, "system/pbstore/app")) continue;
        if (i.path == "system/pbstore/lock" || starts_with(i.path, "system/pbstore/backups")) continue;
        out.push_back(i);
    }
    return out;
}

void copy_tree(const std::string& from, const std::string& to) {
    fs::remove_all(to);
    fs::create_directories(to);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks);
}

const std::vector<std::string> kUnknown = {"--confirm-unknown-model", "--confirm-unknown-firmware", "--confirm-existing-data-folder"};

// A store over an existing tree (copied into the rig's reader folder).
struct StoreOver {
    StoreRig rig;
    std::unique_ptr<StoreService> service;
    CancelToken cancel;
    StoreOver() {}
    void use_tree(const std::string& tree) {
        copy_tree(tree, rig.root());
        rig.make_engine();
        service = rig.make_service();
    }
};

}  // namespace

PB_TEST(an_install_cut_by_the_installer_is_repaired_by_the_store_to_the_same_tree_as_the_installers_own_repair) {
    int compared = 0;
    for (int stop : {8, 16, 25, 34, 45}) {
        FakeReader base;
        base.add_data_dir("testapp", {{"notes.db", "precious"}});
        CliRig cli(base);
        StoreOver store;
        if (!cli.ready() || !store.rig.ready()) PB_SKIP("openssl CLI or the test program is not available");
        cli.host.add_app(nested_sample_app("testapp", "1.2.0"));
        cli.host.publish(100);
        cli.write_reader();
        std::vector<std::string> args = {"install", "testapp", "--reader", cli.root(), "--confirm-plan"};
        args.insert(args.end(), kUnknown.begin(), kUnknown.end());
        RunResult cut = cli.run(args, {"PBSTORE_TEST_STOP_AFTER=" + std::to_string(stop)});
        if (cut.code == 0) continue;   // the install was shorter than the cut
        // the same interrupted tree twice
        const std::string installer_copy = cli.dir.path() + "/installer-copy";
        copy_tree(cli.root(), installer_copy);
        store.use_tree(cli.root());
        StartResult start = store.service->start_check();
        const std::string where = "cut after " + std::to_string(stop);
        if (!start.repair_pending) {   // nothing to repair: the cut came before the journal; both trees must still agree
            PB_CHECK(compare(comparable(snapshot(installer_copy)), comparable(snapshot(store.rig.root()))).identical);
            continue;
        }
        PB_CHECK(!store.service->repair_status().text.empty());
        ExecView fixed = store.service->repair(false, store.cancel, nullptr);
        if (!fixed.ok) PB_FAIL_AT(where + ": the store's repair did not finish: " + (fixed.findings.empty() ? "" : fixed.findings[0].message));
        RunResult theirs = cli.run({"repair", "--reader", installer_copy, "--confirm-repair"});
        PB_CHECK_EQ(theirs.code, 0);
        Diff d = compare(comparable(snapshot(installer_copy)), comparable(snapshot(store.rig.root())));
        if (!d.identical) PB_FAIL_AT(where + ": the store's repair and the installer's repair leave different trees:\n" + d.describe());
        ++compared;
    }
    PB_CHECK(compared >= 3);   // the cuts really interrupted the install
}

PB_TEST(an_install_cut_by_a_power_loss_in_the_store_is_repaired_by_the_installer_to_the_same_tree_as_the_stores_own_repair) {
    int compared = 0;
    for (int cut : {4, 9, 15, 22, 31}) {
        FakeReader base;
        base.add_data_dir("testapp", {{"notes.db", "precious"}});
        CliRig cli(base);
        StoreRig rig(base);
        if (!cli.ready() || !rig.ready()) PB_SKIP("openssl CLI or the test program is not available");
        rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        auto service = rig.make_service();
        service->start_check();
        CancelToken c;
        PB_CHECK(service->refresh(c, nullptr).ok);
        const ReaderFsFactory normal = rig.storage.open_fs;
        rig.storage.open_fs = [cut, normal](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<ThrowingFs>(normal(root), cut); };
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
        PlanView plan = service->plan_action(RowAction::Install, "testapp", c, nullptr);
        PB_CHECK(plan.ok);
        bool lost = false;
        std::vector<std::string> given;
        for (const ConfirmationItem& i : plan.confirmations) given.push_back(i.name);
        try { service->execute_plan(given, false, c, nullptr); } catch (const PowerLoss&) { lost = true; }
        if (!lost) continue;
        rig.storage.open_fs = normal;
        const std::string installer_copy = cli.dir.path() + "/installer-copy";
        copy_tree(rig.root(), installer_copy);
        rig.make_engine();
        auto next = rig.make_service();
        StartResult start = next->start_check();
        const std::string where = "power lost at call " + std::to_string(cut);
        if (!start.repair_pending) {
            PB_CHECK(compare(comparable(snapshot(installer_copy)), comparable(snapshot(rig.root()))).identical);
            continue;
        }
        ExecView fixed = next->repair(false, c, nullptr);
        if (!fixed.ok) PB_FAIL_AT(where + ": the store's repair did not finish: " + (fixed.findings.empty() ? "" : fixed.findings[0].message));
        RunResult theirs = cli.run({"repair", "--reader", installer_copy, "--confirm-repair"});
        if (theirs.code != 0) PB_FAIL_AT(where + ": the installer's repair failed (" + std::to_string(theirs.code) + "): " + theirs.err);
        Diff d = compare(comparable(snapshot(installer_copy)), comparable(snapshot(rig.root())));
        if (!d.identical) PB_FAIL_AT(where + ": the installer's repair and the store's repair leave different trees:\n" + d.describe());
        ++compared;
    }
    PB_CHECK(compared >= 3);
}

PB_TEST_MAIN()
