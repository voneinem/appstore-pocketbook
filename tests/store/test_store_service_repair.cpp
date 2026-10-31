// T081, T083 (story 6, FR-030, FR-034): an interrupted operation found at the start. The start sequence is fixed: the journal check first, then the
// note of a cancelled action, then the list; nothing is written and no network is used until the user confirms the repair. Declining leaves
// the store in browse-only mode (every write action refuses with E-INS-23). The repair needs only 10 % battery and never touches Wi-Fi. A path
// that matches neither state is left alone and named (E-INS-24, the row says "needs repair"); a journal of a newer format is not touched
// (E-INS-35). The engine's own matrix is tests/integration/test_fault_matrix.cpp; tests/store/test_store_fault_matrix.cpp cuts at every call.
#include <fstream>

#include "pbstore/sha256.h"
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
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct RepairKind {
    const char* name;
    FakeReader reader;
    RowAction action;
    bool offer_v2;
};

std::vector<RepairKind> kinds() {
    std::vector<RepairKind> k;
    FakeReader data;
    data.add_data_dir("testapp", {{"notes.db", "precious"}});
    k.push_back({"install", data, RowAction::Install, false});
    k.push_back({"update", with_installed(data, nested_sample_app("testapp", "1.2.0")), RowAction::Update, true});
    k.push_back({"remove", with_installed(data, nested_sample_app("testapp", "1.2.0")), RowAction::Remove, false});
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.2.0"));
    k.push_back({"adopt", hand, RowAction::Check, false});
    return k;
}

struct Rig {
    StoreRig rig;
    RepairKind kind;
    std::unique_ptr<StoreService> service;
    CancelToken cancel;
    explicit Rig(const RepairKind& k) : rig(k.reader), kind(k) {}

    bool prepare() {
        if (!rig.ready()) return false;
        if (kind.offer_v2) {
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
        rig.radio = FakeRadio();
        return true;
    }
    // Leaves the operation of the kind cut by a power loss at the cut-th write. Returns false if the cut came after the end.
    bool interrupt(int cut) {
        const ReaderFsFactory normal = rig.storage.open_fs;
        rig.storage.open_fs = [cut, normal](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<ThrowingFs>(normal(root), cut); };
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
        PlanView plan = service->plan_action(kind.action, "testapp", cancel, nullptr);
        if (!plan.ok) throw std::runtime_error("fixture: no plan");
        std::vector<std::string> given;
        for (const ConfirmationItem& i : plan.confirmations) given.push_back(i.name);
        bool lost = false;
        try { service->execute_plan(given, false, cancel, nullptr); } catch (const PowerLoss&) { lost = true; }
        rig.storage.open_fs = normal;
        return lost;
    }
    // The next start of the store, the cable back.
    StartResult restart() {
        rig.make_engine();
        service = rig.make_service();
        return service->start_check();
    }
};

// A reader left by a cut in the middle of the write with the journal present: the first cut from `from` on that leaves a repair pending.
// The returned rig has restarted (the cable is back); nullptr if no cut did.
std::unique_ptr<Rig> make_interrupted(const RepairKind& k, int from = 10) {
    for (int cut = from; cut < from + 60; ++cut) {
        auto r = std::make_unique<Rig>(k);
        if (!r->prepare()) return nullptr;
        if (!r->interrupt(cut)) return nullptr;
        if (r->restart().repair_pending) {
            r->rig.radio = FakeRadio();   // forget the Wi-Fi of the plan that was cut
            return r;
        }
    }
    return nullptr;
}

bool environment_ready() { return StoreRig().ready(); }

}  // namespace

PB_TEST(the_start_finds_the_interrupted_operation_first_and_changes_nothing_and_uses_no_network_until_the_repair_is_confirmed) {
    for (const RepairKind& k : kinds()) {
        if (!environment_ready()) PB_SKIP("openssl CLI not available");
        auto rp = make_interrupted(k);
        PB_CHECK(rp != nullptr);
        Rig& r = *rp;
        const Snapshot before = snapshot(r.rig.root());
        const int attempts = r.rig.connection_attempts();
        r.rig.radio = FakeRadio();
        StartResult start = r.restart();
        PB_CHECK(start.repair_pending);
        RepairStatus status = r.service->repair_status();
        PB_CHECK(status.pending);
        PB_CHECK(!status.text.empty());                                  // the engine's repair plan, shown as it is
        PB_CHECK(status.action == "finish" || status.action == "undo");
        PB_CHECK(!status.app_id.empty() || status.kind.empty());
        // the list is still there (browsing works), with no action on any row until the repair
        for (const auto& row : r.service->list_cached().rows) PB_CHECK(row.actions.empty());
        PB_CHECK(r.service->settings_facts().catalog_url.size() > 0);
        // nothing but the store's state folder changed, and no network and no radio were used
        Snapshot now = snapshot(r.rig.root());
        for (const auto& d : compare(before, now).added)
            if (d.compare(0, 18, "system/pbstore/app") != 0) PB_FAIL_AT(std::string(k.name) + ": the start wrote " + d);
        PB_CHECK(compare(before, now).removed.empty());
        for (const auto& d : compare(before, now).changed)
            if (d.compare(0, 18, "system/pbstore/app") != 0) PB_FAIL_AT(std::string(k.name) + ": the start changed " + d);
        PB_CHECK_EQ(r.rig.connection_attempts(), attempts);
        PB_CHECK(r.rig.radio.calls().empty());
    }
}

PB_TEST(declining_the_repair_leaves_browse_only_every_write_action_refuses_and_the_repair_stays_possible) {
    for (const RepairKind& k : kinds()) {
        if (!environment_ready()) PB_SKIP("openssl CLI not available");
        auto rp = make_interrupted(k);
        PB_CHECK(rp != nullptr);
        Rig& r = *rp;
        PB_CHECK(r.restart().repair_pending);
        const Snapshot before = snapshot(r.rig.root());
        // "not now": the front end simply does not call repair(); every action is refused with E-INS-23
        for (RowAction a : {RowAction::Install, RowAction::Update, RowAction::Remove, RowAction::Check}) {
            PlanView p = r.service->plan_action(a, "testapp", r.cancel, nullptr);
            PB_CHECK(!p.ok);
            PB_CHECK(has_rule(p.findings, "E-INS-23"));
            PB_CHECK(has(p.messages()[0].text(), "Open PB Store again") || has(p.messages()[0].text(), "repair"));
        }
        PB_CHECK(compare(before, snapshot(r.rig.root())).identical);
        PB_CHECK(r.rig.radio.calls().empty());
        // the repair is still offered, and a later confirmation works
        PB_CHECK(r.service->repair_status().pending);
        ExecView fixed = r.service->repair(false, r.cancel, nullptr);
        PB_CHECK(fixed.ok);
        PB_CHECK(has(fixed.text, "finished") || has(fixed.text, "undone") || has(fixed.text, "interrupted"));
        PB_CHECK(!r.restart().repair_pending);
    }
}

PB_TEST(the_repair_needs_ten_percent_of_battery_and_never_touches_wifi) {
    if (!environment_ready()) PB_SKIP("openssl CLI not available");
    auto rp = make_interrupted(kinds()[0]);
    PB_CHECK(rp != nullptr);
    Rig& r = *rp;
    PB_CHECK(r.restart().repair_pending);
    const Snapshot before = snapshot(r.rig.root());
    r.rig.battery.set_percent(9);
    ExecView low = r.service->repair(false, r.cancel, nullptr);
    PB_CHECK(!low.ok);
    PB_CHECK(has_rule(low.findings, "E-STO-01"));
    PB_CHECK(compare(before, snapshot(r.rig.root())).identical);        // no write below the minimum
    r.rig.battery.set_percent(10);
    PB_CHECK(r.service->repair(false, r.cancel, nullptr).ok);
    PB_CHECK(r.rig.radio.calls().empty());                              // the Wi-Fi is never touched
}

PB_TEST(a_path_that_matches_neither_state_is_left_alone_and_named_and_the_row_says_needs_repair) {
    if (!environment_ready()) PB_SKIP("openssl CLI not available");
    bool found = false;
    for (int from : {16, 24, 32, 40, 48}) {   // an update: some cut leaves a file in the way of the repair
        auto rp = make_interrupted(kinds()[1], from);
        if (!rp) continue;
        Rig& r = *rp;
        // someone changed every file of the app to something that is neither the old nor the new content
        const std::string junk = "neither the old nor the new content";
        std::vector<std::string> files;
        for (const char* f : {"applications/testapp.bin", "applications/testapp.app", "applications/testapp/lib/core.bin", "applications/testapp/lib/extra/data.txt", "applications/icons/testapp_f.bmp"})
            if (!slurp(r.rig.root() + "/" + f).empty()) {
                std::ofstream o(r.rig.root() + "/" + f, std::ios::trunc);
                o << junk;
                files.push_back(f);
            }
        PB_CHECK(r.restart().repair_pending);
        ExecView fixed = r.service->repair(false, r.cancel, nullptr);
        if (fixed.ok) continue;
        PB_CHECK(has_rule(fixed.findings, "E-INS-24"));
        for (const std::string& f : files) {   // left alone: whatever it is now, it is still the junk or a complete earlier content, never touched half-way
            const std::string now = slurp(r.rig.root() + "/" + f);
            PB_CHECK(!now.empty());
        }
        bool noted = false;
        for (const auto& row : fixed.view.rows)
            if (row.id == "testapp") for (const auto& n : row.notes) noted = noted || has(n, "needs repair");
        PB_CHECK(noted);
        found = true;
        break;
    }
    PB_CHECK(found);
}

PB_TEST(a_journal_of_a_newer_format_is_left_untouched_and_the_store_says_it_needs_updating) {
    if (!environment_ready()) PB_SKIP("openssl CLI not available");
    auto rp = make_interrupted(kinds()[0]);
    PB_CHECK(rp != nullptr);
    Rig& r = *rp;
    const std::string intent = r.rig.root() + "/system/pbstore/journal/current/10-intent.json";
    std::string text = slurp(intent);
    const size_t at = text.find("\"journal_format\"");
    PB_CHECK(at != std::string::npos);
    const size_t digit = text.find_first_of("0123456789", at);
    text[digit] = '9';                                                    // a format this program does not know ...
    const std::string body = text.substr(0, text.size() - (7 + 64 + 1));
    text = body + "sha256:" + sha256_hex(body) + "\n";                      // ... with a checksum that is right
    { std::ofstream f(intent, std::ios::trunc); f << text; }
    const Snapshot before = snapshot(r.rig.root());
    StartResult start = r.restart();
    if (start.repair_pending) {
        ExecView out = r.service->repair(false, r.cancel, nullptr);
        PB_CHECK(!out.ok);
        PB_CHECK(has_rule(out.findings, "E-INS-35"));
        PB_CHECK(has(out.messages()[0].next, "needs updating"));
    }
    Snapshot now = snapshot(r.rig.root());
    for (const auto& d : compare(before, now).added) PB_CHECK(d.compare(0, 18, "system/pbstore/app") == 0);
    PB_CHECK(compare(before, now).removed.empty());
    PB_CHECK(slurp(intent) == text);                                      // untouched
}

PB_TEST(without_a_journal_the_start_goes_straight_to_the_list_and_a_note_is_reported_once_as_cancelled) {
    Rig r(kinds()[0]);
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    // the user pressed home before the first write: nothing was started, no note; the start has nothing to say
    StartResult plain = r.restart();
    PB_CHECK(!plain.repair_pending);
    PB_CHECK(plain.notices.empty());
    // a note left by an action that was undone: reported once, after the (empty) journal check
    r.rig.pending_note->write({"install", "testapp", "", "1.2.0"});
    StartResult noted = r.restart();
    PB_CHECK(!noted.repair_pending);
    PB_CHECK(has_rule(noted.notices, "W-STO-01"));
    PB_CHECK(!has_rule(r.restart().notices, "W-STO-01"));                 // once
    // with a journal, the repair speaks and the note is dropped without a word
    auto jp = make_interrupted(kinds()[0]);
    PB_CHECK(jp != nullptr);
    Rig& j = *jp;
    j.rig.pending_note->write({"install", "testapp", "", "1.2.0"});
    StartResult withj = j.restart();
    PB_CHECK(withj.repair_pending);
    PB_CHECK(!has_rule(withj.notices, "W-STO-01"));
    PB_CHECK(j.rig.storage.state->size("pending-note.txt") == UINT64_MAX);
}

PB_TEST_MAIN()
