// T103a (spec FR-040): where the store may write. Every mutating call of a cycle on the reader - refresh, install, update, remove, adopt, a
// repair after an interruption, a cancelled install, and the confirmed deletion of the data folder - is recorded (tests/support/recording_fs.h
// wraps the reader's file layer and the state folder) and must lie inside applications/, the one system/config/desktop/view.json, or
// system/pbstore/** (and in system/pbstore/app/ only the names of state_names.h). A data folder system/<data_dir> is written only by the
// confirmed data deletion; hash.txt and every other vendor entry are never written.
#include "recording_fs.h"
#include "store_rig.h"
#include "test_support.h"
#include "pbstore/store/state_names.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

// Why a path may not be written, or "" if it may.
std::string forbidden(const std::string& path, bool data_deletion) {
    if (starts_with(path, "applications/") || path == "applications") return "";
    if (path == "system/config/desktop/view.json") return "";
    if (starts_with(path, "system/pbstore/app/")) {
        const std::string rest = path.substr(std::string("system/pbstore/app/").size());
        const std::string name = rest.substr(0, rest.find('/'));
        if (rest.find('/') == std::string::npos && is_store_name(name)) return "";
        if (starts_with(rest, "run/")) return "";   // the launcher's folder, written through the same wrapper only by the store's RunState
        return "a name that is not in state_names.h";
    }
    if (path == "system/pbstore" || starts_with(path, "system/pbstore/")) return "";
    if (data_deletion && (path == "system/testapp" || starts_with(path, "system/testapp/"))) return "";
    if (starts_with(path, "system/testapp")) return "a data folder, written without the confirmed deletion";
    if (starts_with(path, "system/config/desktop/")) return "a vendor entry of the desktop configuration";
    return "outside the places the store may write";
}

void check_scope(const Record& rec, bool data_deletion, const std::string& where) {
    for (const Record::Call& c : rec.calls)
        for (const std::string& p : {c.path, c.target}) {
            if (p.empty()) continue;
            const std::string why = forbidden(p, data_deletion);
            if (!why.empty()) PB_FAIL_AT(where + ": " + c.op + " " + p + " - " + why);
        }
}

struct Scoped {
    StoreRig rig;
    std::shared_ptr<Record> rec = std::make_shared<Record>();
    std::unique_ptr<StoreService> service;
    CancelToken first_token, second_token;
    CancelToken* cancel = &first_token;
    std::vector<std::string> given;

    explicit Scoped(const FakeReader& r = FakeReader()) : rig(r) {}

    bool prepare(bool offer_v2 = false) {
        if (!rig.ready()) return false;
        if (offer_v2) {
            CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
            e.listed_versions = {"1.2.0", "1.3.0"};
        } else {
            rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        }
        rig.host.publish(100);
        rig.write_reader();
        // record the reader's file layer and the state folder
        const ReaderFsFactory normal = rig.storage.open_fs;
        std::shared_ptr<Record> r = rec;
        rig.storage.open_fs = [normal, r](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<RecordingFs>(normal(root), r); };
        rig.storage.state = std::make_unique<RecordingStateDirs>(std::move(rig.storage.state), rec, "system/pbstore/app/");
        rig.log = std::make_unique<Log>(*rig.storage.state, rig.clock, rig.redactor, "pbstore.log");
        rig.cache = std::make_unique<StoreCatalogCache>(*rig.storage.state, rig.clock);
        rig.pending_note = std::make_unique<PendingNote>(*rig.storage.state);
        rig.run_state = std::make_unique<RunState>(*rig.storage.state, *rig.run_dirs);
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
        return true;
    }
    void fresh_start() {
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
    }
    PlanView plan(RowAction a, const std::string& id = "testapp") {
        PlanView p = service->plan_action(a, id, *cancel, nullptr);
        given.clear();
        for (const ConfirmationItem& i : p.confirmations) given.push_back(i.name);
        return p;
    }
    ExecView execute() { return service->execute_plan(given, false, *cancel, nullptr); }
};

}  // namespace

PB_TEST(a_refresh_and_an_install_write_only_where_the_store_may) {
    Scoped s;
    if (!s.prepare()) PB_SKIP("openssl CLI not available");
    CancelToken c;
    PB_CHECK(s.service->refresh(c, nullptr).ok);
    PB_CHECK(!s.rec->calls.empty());                 // the cache, the log and the serial memory were written
    check_scope(*s.rec, false, "refresh");
    s.rec->clear();
    PB_CHECK(s.plan(RowAction::Install).ok);
    check_scope(*s.rec, false, "install plan");
    PB_CHECK(s.execute().ok);
    PB_CHECK(!s.rec->calls.empty());
    check_scope(*s.rec, false, "install");
    // the checker itself: a write outside the places is found
    Record bad;
    bad.calls.push_back({"create", "system/config/desktop/hash.txt", ""});
    bad.calls.push_back({"rename", "applications/a", "system/other"});
    PB_CHECK(!forbidden("system/config/desktop/hash.txt", false).empty());
    PB_CHECK(!forbidden("system/other", false).empty());
    PB_CHECK(!forbidden("system/pbstore/app/not-a-name.bin", false).empty());
    PB_CHECK(forbidden("system/pbstore/app/pbstore.log", false).empty());
    PB_CHECK(forbidden("system/pbstore/installed/testapp.json", false).empty());
}

PB_TEST(an_update_writes_only_where_the_store_may_and_never_the_data_folder) {
    FakeReader base = with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0"));
    base.add_data_dir("testapp", {{"user.txt", "my data"}});
    Scoped s(base);
    if (!s.prepare(true)) PB_SKIP("openssl CLI not available");
    CancelToken c;
    PB_CHECK(s.service->refresh(c, nullptr).ok);
    s.rec->clear();
    PB_CHECK(s.plan(RowAction::Update).ok);
    PB_CHECK(s.execute().ok);
    PB_CHECK(!s.rec->calls.empty());
    check_scope(*s.rec, false, "update");
}

PB_TEST(a_removal_leaves_the_data_folder_alone_and_only_the_confirmed_deletion_writes_it) {
    FakeReader base = with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0"));
    base.add_data_dir("testapp", {{"user.txt", "my data"}, {"sub/more.txt", "more"}});
    Scoped s(base);
    if (!s.prepare()) PB_SKIP("openssl CLI not available");
    s.rec->clear();
    PB_CHECK(s.plan(RowAction::Remove).ok);
    ExecView out = s.execute();
    PB_CHECK(out.ok);
    PB_CHECK(out.data_folder_left);
    PB_CHECK(!s.rec->calls.empty());
    check_scope(*s.rec, false, "remove");
    s.rec->clear();
    PB_CHECK(s.service->plan_delete_data().ok);
    PB_CHECK(s.service->execute_delete_data(*s.cancel, nullptr).ok);
    bool touched_data = false;
    for (const Record::Call& call : s.rec->calls) touched_data = touched_data || starts_with(call.path, "system/testapp");
    PB_CHECK(touched_data);                          // the deletion really writes the data folder ...
    check_scope(*s.rec, true, "delete data");        // ... and nothing else outside the places
}

PB_TEST(adopting_a_hand_installed_app_writes_only_the_record) {
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.2.0"));
    hand.add_data_dir("testapp", {{"user.txt", "my data"}});
    Scoped s(hand);
    if (!s.prepare()) PB_SKIP("openssl CLI not available");
    CancelToken c;
    PB_CHECK(s.service->refresh(c, nullptr).ok);
    s.rec->clear();
    PB_CHECK(s.plan(RowAction::Check).ok);
    PB_CHECK(s.execute().ok);
    check_scope(*s.rec, false, "adopt");
    for (const Record::Call& call : s.rec->calls)    // not one file of the app or the desktop configuration
        PB_CHECK(!starts_with(call.path, "applications/") && call.path != "system/config/desktop/view.json");
}

PB_TEST(a_cancelled_install_and_the_repair_after_a_power_loss_write_only_where_the_store_may) {
    Scoped s;
    if (!s.prepare()) PB_SKIP("openssl CLI not available");
    CancelToken c;
    PB_CHECK(s.service->refresh(c, nullptr).ok);
    // a cancel before the first write
    s.rec->clear();
    PB_CHECK(s.plan(RowAction::Install).ok);
    s.cancel->cancel();
    ExecView cancelled = s.execute();
    PB_CHECK(!cancelled.ok);
    check_scope(*s.rec, false, "cancelled install");
    s.cancel = &s.second_token;
    // the power is lost in the middle of the install, the next start repairs
    s.rec->clear();
    PB_CHECK(s.plan(RowAction::Install).ok);
    s.rec->lose_at = 20;
    bool lost = false;
    try { s.execute(); } catch (const PowerLoss&) { lost = true; }
    PB_CHECK(lost);
    check_scope(*s.rec, false, "install cut by a power loss");
    s.rec->clear();
    s.rec->lose_at = 0;
    s.fresh_start();
    PB_CHECK(s.service->repair_status().pending);
    PB_CHECK(s.service->repair(false, *s.cancel, nullptr).ok);
    PB_CHECK(!s.rec->calls.empty());
    check_scope(*s.rec, false, "repair");
}

PB_TEST_MAIN()
