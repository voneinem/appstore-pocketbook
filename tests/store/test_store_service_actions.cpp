// The write actions of the store (stories 2 to 6): install, update, remove (also without a network), deleting the data folder, adopting
// a hand-installed app, and repairing an interrupted operation. Everything goes through the engine of feature 002; these tests prove that the
// store adds only the gates, the Wi-Fi session and the note, and that every refusal leaves the reader byte-identical.
#include <sys/stat.h>

#include <algorithm>
#include <fstream>

#include "store_rig.h"
#include "test_support.h"
#include "throwing_fs.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
const AppRow* find(const std::vector<AppRow>& rows, const std::string& id) {
    for (const auto& r : rows) if (r.id == id) return &r;
    return nullptr;
}

// the reader's files without the store's own state, the journal's folder and the log (they differ between the store and the engine alone)
Snapshot apps_only(const Snapshot& s, bool keep_record = true) {
    Snapshot out;
    for (const auto& i : s) {
        if (i.path.compare(0, 18, "system/pbstore/app") == 0) continue;
        if (i.path == "system/pbstore/lock" || i.path == "system/pbstore/journal") continue;       // the engine's own lock and an empty journal folder
        if (i.path == "system/pbstore/installed" && i.kind == 'd') continue;
        if (i.path.compare(0, 22, "system/pbstore/backups") == 0) continue;               // an undone operation keeps the backup of view.json it made
        if (!keep_record && i.path.compare(0, 27, "system/pbstore/installed/") == 0) continue;
        out.push_back(i);
    }
    return out;
}

// PB Store itself: no data folder of its own (spec FR-041)
SampleApp store_app(const std::string& version, unsigned seed = 1) { return make_sample_app("pbstore", version, "U_PbStore", "", seed); }

const std::vector<std::string> kUnknown = {"unknown-model", "unknown-firmware"};   // the store cannot read the reader's model and firmware yet

struct Rig {
    StoreRig rig;
    std::unique_ptr<StoreService> service;
    CancelToken cancel;
    explicit Rig(const FakeReader& r = FakeReader()) : rig(r) {}
    void prepare(bool with_v2 = false) {
        if (!rig.ready()) return;
        if (with_v2) {
            CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
            e.listed_versions = {"1.2.0", "1.3.0"};
        } else {
            rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        }
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
        CancelToken c;
        if (!service->refresh(c, nullptr).ok) throw std::runtime_error("fixture: refresh failed");
        rig.radio = FakeRadio();               // forget the radio calls of the refresh
    }
};

}  // namespace

PB_TEST(install_downloads_shows_the_engines_plan_writes_only_after_the_confirmation_and_restores_wifi) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    Snapshot before = snapshot(r.rig.root());
    PlanView plan = r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr);
    PB_CHECK(plan.ok);
    PB_CHECK(has(plan.text, "Plan: Install"));
    PB_CHECK(!plan.confirmations.empty());                                 // the unknown model and firmware
    PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);   // nothing written before the confirmation
    PB_CHECK_EQ(r.rig.radio.count("connect"), 1);                          // Wi-Fi was needed for the download ...
    PB_CHECK(r.rig.radio.is_on() == std::optional<bool>(false));           // ... and is off again
    PB_CHECK(!has_rule(plan.notices, "W-STO-03"));
    ExecView done = r.service->execute_plan(kUnknown, false, r.cancel, nullptr);
    PB_CHECK(done.ok);
    PB_CHECK(has(done.text, "is installed"));
    PB_CHECK(!has(done.text, "Wi-Fi"));                                  // the switch is not announced (2026-10-29)
    PB_CHECK(r.rig.storage.state->size("pending-note.txt") == UINT64_MAX);   // the note is gone after a finished result
    PB_CHECK(find(done.view.rows, "testapp")->state == RowState::Installed);
    std::ifstream rec(r.rig.root() + "/system/pbstore/installed/testapp.json");
    std::string text((std::istreambuf_iterator<char>(rec)), std::istreambuf_iterator<char>());
    PB_CHECK(has(text, "pbstore-app 0.1.0"));                              // the record says which program wrote it
}

PB_TEST(the_same_package_gives_the_same_plan_text_and_the_same_reader_as_the_engine_alone_apart_from_installed_by) {
    Rig a, b;
    if (!a.rig.ready()) PB_SKIP("openssl CLI not available");
    a.prepare();
    // the engine alone, on an equal reader, with the same clock and the same catalog
    b.rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    b.rig.host.publish(100);
    b.rig.write_reader();
    Engine& engine = b.rig.make_engine();
    auto handle = engine.open_reader(b.rig.root());
    Action act;
    act.type = Action::Type::Install;
    act.app_id = "testapp";
    CancelToken c;
    auto planned = engine.plan(**handle.value, act, nullptr, c);
    PB_CHECK(planned.ok());
    PlanView via_store = a.service->plan_action(RowAction::Install, "testapp", a.cancel, nullptr);
    PB_CHECK(via_store.ok);
    PB_CHECK_EQ(via_store.text, planned.value->text);                      // equal plan text (spec FR-014, SC-002)
    Confirmations given = planned.value->plan.confirmations;
    PB_CHECK(engine.execute(**handle.value, *planned.value, given, nullptr, c).ok());
    PB_CHECK(a.service->execute_plan(kUnknown, false, a.cancel, nullptr).ok);
    // equal reader contents; the one allowed difference is the program name in the record
    PB_CHECK(compare(apps_only(snapshot(a.rig.root()), false), apps_only(snapshot(b.rig.root()), false)).identical);
}

PB_TEST(refusals_leave_the_reader_byte_identical_with_three_part_messages) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    Snapshot before = snapshot(r.rig.root());
    // a low battery: refused before anything is downloaded
    r.rig.battery.set_percent(20);
    PlanView low = r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr);
    PB_CHECK(!low.ok);
    PB_CHECK(has_rule(low.findings, "E-STO-01"));
    PB_CHECK_EQ(r.rig.radio.count("connect"), 0);
    PB_CHECK(has(low.messages()[0].text(), "not changed"));
    r.rig.battery.set_percent(80);
    // too little memory for the package
    r.rig.memory.set_available(1 << 20);
    PlanView mem = r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr);
    PB_CHECK(has_rule(mem.findings, "E-STO-03"));
    r.rig.memory.set_available(512ull << 20);
    // a package that does not match its signed hash
    r.rig.host.files["/voneinem/appstore-pocketbook/releases/download/testapp-1.2.0/testapp.zip"] = "not the package";
    for (auto& kv : r.rig.host.files) if (kv.first.find(".zip") != std::string::npos) kv.second = "not the package";
    PlanView hash = r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr);
    PB_CHECK(!hash.ok);
    PB_CHECK(!hash.findings.empty());
    PB_CHECK(r.rig.radio.is_on() == std::optional<bool>(false));           // Wi-Fi restored also after a failure
    // a battery whose level cannot be read needs the explicit tap
    for (auto& kv : r.rig.host.files) if (kv.first.find(".zip") != std::string::npos) kv.second = build_package(nested_sample_app("testapp", "1.2.0"));
    r.rig.battery.set_percent_unreadable();
    PlanView risk = r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr);
    PB_CHECK(risk.ok);
    PB_CHECK(risk.battery_unreadable);
    ExecView refused = r.service->execute_plan(kUnknown, false, r.cancel, nullptr);
    PB_CHECK(!refused.ok);
    PB_CHECK(has_rule(refused.findings, "E-STO-05"));
    PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);
}

PB_TEST(a_battery_that_falls_below_the_minimum_after_the_plan_stops_the_write) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    Snapshot before = snapshot(r.rig.root());
    PlanView plan = r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr);
    PB_CHECK(plan.ok);
    r.rig.battery.set_percent(10);                                         // checked again immediately before the first write
    ExecView out = r.service->execute_plan(kUnknown, false, r.cancel, nullptr);
    PB_CHECK(!out.ok);
    PB_CHECK(has_rule(out.findings, "E-STO-01"));
    PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);
}

PB_TEST(a_missing_consent_writes_nothing) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    Snapshot before = snapshot(r.rig.root());
    PB_CHECK(r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr).ok);
    ExecView out = r.service->execute_plan({}, false, r.cancel, nullptr);   // the unknown model and firmware were not confirmed
    PB_CHECK(!out.ok);
    PB_CHECK(!out.findings.empty());
    PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);
}

PB_TEST(update_keeps_the_data_folder_and_says_so) {
    Rig r(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare(true);
    ::mkdir((r.rig.root() + "/system/testapp").c_str(), 0755);
    { std::ofstream f(r.rig.root() + "/system/testapp/user.txt"); f << "my data"; }
    PB_CHECK(find(r.service->rows(""), "testapp")->state == RowState::UpdateAvailable);
    PlanView plan = r.service->plan_action(RowAction::Update, "testapp", r.cancel, nullptr);
    PB_CHECK(plan.ok);
    ExecView out = r.service->execute_plan(kUnknown, false, r.cancel, nullptr);
    PB_CHECK(out.ok);
    PB_CHECK(has(out.text, "Its data was kept"));
    PB_CHECK(find(out.view.rows, "testapp")->state == RowState::Installed);
    PB_CHECK_EQ(find(out.view.rows, "testapp")->installed_version, std::string("1.3.0"));
    std::ifstream f(r.rig.root() + "/system/testapp/user.txt");
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    PB_CHECK_EQ(data, std::string("my data"));
}

PB_TEST(remove_works_without_any_network_keeps_the_data_folder_and_can_delete_it_on_request) {
    Rig r(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    ::mkdir((r.rig.root() + "/system/testapp").c_str(), 0755);
    { std::ofstream f(r.rig.root() + "/system/testapp/user.txt"); f << "my data"; }
    r.rig.radio = FakeRadio();
    const int attempts = r.rig.connection_attempts();
    PlanView plan = r.service->plan_action(RowAction::Remove, "testapp", r.cancel, nullptr);
    PB_CHECK(plan.ok);
    PB_CHECK_EQ(plan.data_folder, std::string("system/testapp"));
    PB_CHECK_EQ(plan.data_files, uint64_t(1));
    ExecView out = r.service->execute_plan(kUnknown, false, r.cancel, nullptr);
    PB_CHECK(out.ok);
    PB_CHECK(has(out.text, "was removed"));
    PB_CHECK(has(out.text, "Its data was kept in system/testapp"));
    PB_CHECK(out.data_folder_left);
    PB_CHECK(r.rig.radio.calls().empty());                                  // no Wi-Fi for a removal
    PB_CHECK_EQ(r.rig.connection_attempts(), attempts);
    struct stat st;
    PB_CHECK_EQ(::stat((r.rig.root() + "/applications/testapp").c_str(), &st), -1);   // the app's files are gone
    PB_CHECK_EQ(::stat((r.rig.root() + "/system/testapp/user.txt").c_str(), &st), 0);   // the data stays
    // the separate, confirmed deletion of the data folder
    PlanView del = r.service->plan_delete_data();
    PB_CHECK(del.ok);
    PB_CHECK_EQ(del.data_folder, std::string("system/testapp"));
    ExecView gone = r.service->execute_delete_data(r.cancel, nullptr);
    PB_CHECK(gone.ok);
    PB_CHECK_EQ(::stat((r.rig.root() + "/system/testapp").c_str(), &st), -1);
}

PB_TEST(pb_store_does_not_remove_itself) {
    Rig r(with_installed(FakeReader(), store_app("0.0.1")));
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    Snapshot before = snapshot(r.rig.root());
    PlanView plan = r.service->plan_action(RowAction::Remove, "pbstore", r.cancel, nullptr);
    PB_CHECK(!plan.ok);
    PB_CHECK(has_rule(plan.findings, "E-STO-08"));
    PB_CHECK(has(plan.messages()[0].text(), "PB Installer on a computer"));
    PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);
}

PB_TEST(a_leave_request_keeps_the_note_so_the_next_start_says_what_happened) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    PB_CHECK(r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr).ok);
    r.service->request_leave(LeaveReason::Home);
    r.service->execute_plan(kUnknown, false, r.cancel, nullptr);
    PB_CHECK(r.rig.storage.state->size("pending-note.txt") != UINT64_MAX);
    auto again = r.rig.make_service();
    StartResult start = again->start_check();
    PB_CHECK(has_rule(start.notices, "W-STO-01"));
}

PB_TEST(adopt_checks_the_files_and_writes_only_the_install_record) {
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.2.0"));
    Rig r(hand);
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    PB_CHECK(find(r.service->rows(""), "testapp")->state == RowState::InstalledOutside);
    Snapshot before = snapshot(r.rig.root());
    PlanView plan = r.service->plan_action(RowAction::Check, "testapp", r.cancel, nullptr);
    PB_CHECK(plan.ok);
    PB_CHECK(has(plan.text, "Adopt"));
    ExecView out = r.service->execute_plan(kUnknown, false, r.cancel, nullptr);
    PB_CHECK(out.ok);
    PB_CHECK(has(out.text, "is now managed by PB Store"));
    PB_CHECK(!has(out.text, "restart"));
    Diff d = compare(apps_only(before), apps_only(snapshot(r.rig.root())));
    PB_CHECK(d.removed.empty() && d.changed.empty());
    for (const auto& p : d.added)   // the record, and the engine's own lock file, backup folder and installed folder
        PB_CHECK(p.compare(0, 25, "system/pbstore/installed/") == 0 || p == "system/pbstore/installed" || p == "system/pbstore/lock" || p.compare(0, 16, "system/pbstore/b") == 0);
    PB_CHECK(find(out.view.rows, "testapp")->state == RowState::Installed);
}

PB_TEST(an_interrupted_install_is_found_at_the_next_start_and_repaired_to_the_old_or_the_new_complete_version) {
    // the reference states: before, and after a normal install
    Rig reference;
    if (!reference.rig.ready()) PB_SKIP("openssl CLI not available");
    reference.prepare();
    const Snapshot old_state = apps_only(snapshot(reference.rig.root()), false);
    PB_CHECK(reference.service->plan_action(RowAction::Install, "testapp", reference.cancel, nullptr).ok);
    PB_CHECK(reference.service->execute_plan(kUnknown, false, reference.cancel, nullptr).ok);
    const Snapshot new_state = apps_only(snapshot(reference.rig.root()), false);
    int repaired = 0;
    for (int cut : {3, 8, 14, 20, 27}) {
        Rig r;
        r.prepare();
        // the power is lost at the cut-th write of the install
        const ReaderFsFactory normal = r.rig.storage.open_fs;
        r.rig.storage.open_fs = [cut, normal](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<ThrowingFs>(normal(root), cut); };
        r.rig.make_engine();
        r.service = r.rig.make_service();
        r.service->start_check();
        CancelToken c;
        PB_CHECK(r.service->plan_action(RowAction::Install, "testapp", c, nullptr).ok);
        bool lost = false;
        try { r.service->execute_plan(kUnknown, false, c, nullptr); } catch (const PowerLoss&) { lost = true; }
        if (!lost) continue;
        // the next start, with the cable back: the store finds the operation before anything else
        r.rig.storage.open_fs = normal;
        r.rig.make_engine();
        auto next = r.rig.make_service();
        StartResult start = next->start_check();
        if (!start.repair_pending) {   // the power was lost before the journal existed: nothing was changed, nothing is left to repair
            const Snapshot now = apps_only(snapshot(r.rig.root()), false);
            PB_CHECK(compare(old_state, now).identical || compare(new_state, now).identical);
            continue;
        }
        PB_CHECK(next->repair_status().pending);
        PB_CHECK(!next->repair_status().text.empty());
        // nothing can be started until the repair is done; browsing still works
        PB_CHECK(has_rule(next->plan_action(RowAction::Install, "testapp", c, nullptr).findings, "E-INS-23"));
        for (const auto& row : next->list_cached().rows) PB_CHECK(row.actions.empty());
        ExecView fixed = next->repair(false, c, nullptr);
        PB_CHECK(fixed.ok);
        PB_CHECK(has(fixed.text, "interrupted"));
        const Snapshot now = apps_only(snapshot(r.rig.root()), false);
        const bool old_ok = compare(old_state, now).identical, new_ok = compare(new_state, now).identical;
        if (!(old_ok || new_ok)) PB_FAIL_AT("after the repair the reader holds neither the old nor the new complete version (cut " + std::to_string(cut) + ")");
        PB_CHECK(!next->start_check().repair_pending);                       // nothing is left to repair
        ++repaired;
    }
    PB_CHECK(repaired >= 2);                                                 // the cuts really interrupted the install
}

PB_TEST(update_all_lists_the_apps_with_an_update_and_pb_store_last) {
    FakeReader base = with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0"));
    base = with_installed(base, store_app("0.0.1"));
    Rig r(base);
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    CatalogEntry& e = r.rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
    e.listed_versions = {"1.2.0", "1.3.0"};
    CatalogEntry& s = r.rig.host.add_app(store_app("0.2.0", 77));
    s.listed_versions = {"0.0.1", "0.2.0"};
    r.rig.host.publish(100);
    r.rig.write_reader();
    r.rig.make_engine();
    r.service = r.rig.make_service();
    CancelToken c;
    PB_CHECK(r.service->refresh(c, nullptr).ok);
    const auto queue = r.service->update_all_queue();
    PB_CHECK_EQ(queue.size(), size_t(2));
    PB_CHECK_EQ(queue.front(), std::string("testapp"));
    PB_CHECK_EQ(queue.back(), std::string("pbstore"));
}

PB_TEST(updating_pb_store_itself_is_staged_the_record_says_the_new_version_and_the_text_says_when_it_takes_effect) {
    Rig r(with_installed(FakeReader(), store_app("0.0.1")));
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    CatalogEntry& e = r.rig.host.add_app(store_app("0.2.0", 77));
    e.listed_versions = {"0.0.1", "0.2.0"};
    r.rig.host.publish(100);
    r.rig.write_reader();
    r.rig.make_engine();
    r.service = r.rig.make_service();              // the running version is 0.1.0 (the rig's default)
    r.service->start_check();
    CancelToken c;
    PB_CHECK(r.service->refresh(c, nullptr).ok);
    const AppRow* row = find(r.service->rows(""), "pbstore");
    PB_CHECK(row && row->state == RowState::UpdateAvailable);
    PB_CHECK(row->is_store);
    PB_CHECK(std::find(row->actions.begin(), row->actions.end(), RowAction::Remove) == row->actions.end());   // no Remove
    PlanView plan = r.service->plan_action(RowAction::Update, "pbstore", c, nullptr);
    PB_CHECK(plan.ok);
    PB_CHECK(plan.is_store);
    PB_CHECK(has(plan.extra, "takes effect when you open PB Store again"));
    ExecView out = r.service->execute_plan(kUnknown, false, c, nullptr);
    PB_CHECK(out.ok);
    PB_CHECK(has(out.text, "PB Store 0.2.0 is installed. It takes effect when you open PB Store again."));
    PB_CHECK(!has(out.text, "restart the reader"));
    // the record says 0.2.0 while 0.1.0 still runs: the list says that an update is staged
    const AppRow* after = find(out.view.rows, "pbstore");
    PB_CHECK(after != nullptr);
    PB_CHECK(std::find(after->notes.begin(), after->notes.end(), "update staged: takes effect when you open PB Store again") != after->notes.end());
    // what the launcher leaves when the new version did not start: shown once, and the list says which version runs
    r.rig.run_dirs->write_atomic("message", "update-failed\n");
    r.rig.run_dirs->write_atomic("pbstore.bin.failed", "ELF");
    StartResult start = r.service->start_check();
    PB_CHECK(has_rule(start.notices, "E-STO-06"));
    auto cached = r.service->list_cached();
    const AppRow* rolled = find(cached.rows, "pbstore");
    PB_CHECK(rolled != nullptr);
    bool says_failed = false;
    for (const auto& n : rolled->notes) if (has(n, "failed to start") && has(n, "0.1.0 is running")) says_failed = true;
    PB_CHECK(says_failed);
}

PB_TEST(an_app_put_there_by_hand_that_differs_from_the_catalog_package_is_replaced_after_the_check_says_it_differs) {
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.1.0"));       // another build than the catalog's 1.2.0
    Rig r(hand);
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    std::vector<AppRow> rows = r.service->rows("");
    const AppRow* row = find(rows, "testapp");
    PB_CHECK(row && row->state == RowState::InstalledOutside);
    PB_CHECK(std::find(row->actions.begin(), row->actions.end(), RowAction::Check) != row->actions.end());
    PB_CHECK(std::find(row->actions.begin(), row->actions.end(), RowAction::Replace) != row->actions.end());
    PlanView check = r.service->plan_action(RowAction::Check, "testapp", r.cancel, nullptr);   // the files differ: no adoption
    PB_CHECK(!check.ok);
    PB_CHECK(has_rule(check.findings, "E-INS-20"));
    PlanView plan = r.service->plan_action(RowAction::Replace, "testapp", r.cancel, nullptr);
    PB_CHECK(plan.ok);
    PB_CHECK(has(plan.text, "Plan: Replace"));
    bool asks = false;
    for (const auto& c : plan.confirmations) if (c.name == "replace") asks = true;
    PB_CHECK(asks);
    ExecView missing = r.service->execute_plan(kUnknown, false, r.cancel, nullptr);                 // without the consent to replace
    PB_CHECK(!missing.ok);
    PlanView again = r.service->plan_action(RowAction::Replace, "testapp", r.cancel, nullptr);
    PB_CHECK(again.ok);
    std::vector<std::string> given = kUnknown;
    given.push_back("replace");
    ExecView out = r.service->execute_plan(given, false, r.cancel, nullptr);
    PB_CHECK(out.ok);
    PB_CHECK(has(out.text, "is installed"));
    const AppRow* after = find(out.view.rows, "testapp");
    PB_CHECK(after && after->state == RowState::Installed);
    PB_CHECK(after->installed_version == "1.2.0");
}

PB_TEST_MAIN()
