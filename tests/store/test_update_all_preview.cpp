// Feature 004 (T016, story 2, FR-009): the preview that the combined confirmation of "Update all" shows. It lists the apps of the run in the order
// they will be updated (PB Store last) with both versions and the package size, the total size, the consents that concern the reader itself
// (given once for the whole run), and whether the battery level can be read. It makes no network request and writes nothing, and it is not
// offered while an interrupted operation waits for its repair.
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

// the reader's files without the store's own state, the journal's folder, the lock and the backups (as in test_store_service_actions.cpp)
Snapshot apps_only(const Snapshot& s) {
    Snapshot out;
    for (const auto& i : s) {
        if (i.path.compare(0, 18, "system/pbstore/app") == 0) continue;
        if (i.path == "system/pbstore/lock" || i.path == "system/pbstore/journal") continue;
        if (i.path == "system/pbstore/installed" && i.kind == 'd') continue;
        if (i.path.compare(0, 22, "system/pbstore/backups") == 0) continue;
        out.push_back(i);
    }
    return out;
}

SampleApp store_app(const std::string& version, unsigned seed = 1) { return make_sample_app("pbstore", version, "U_PbStore", "", seed); }

// An identity source that says what the test wants: nothing (unknown), a readable InkPad One, or a firmware string nobody can parse.
class FixedIdentity : public IdentitySource {
public:
    FixedIdentity(std::optional<std::string> model, std::optional<std::string> firmware) : model_(std::move(model)), firmware_(std::move(firmware)) {}
    std::optional<std::string> model(ReaderFs&) override { return model_; }
    std::optional<std::string> firmware(ReaderFs&) override { return firmware_; }

private:
    std::optional<std::string> model_, firmware_;
};

struct Rig {
    StoreRig rig;
    std::unique_ptr<StoreService> service;
    CancelToken cancel;
    explicit Rig(const FakeReader& r = FakeReader()) : rig(r) {}

    // testapp 1.2.0 installed and 1.3.0 offered; with `with_store` PB Store 0.0.1 installed and 0.2.0 offered too
    bool prepare(bool outdated, bool with_store, std::vector<IdentitySource*> sources = {}) {
        if (!rig.ready()) return false;
        if (outdated) {
            CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
            e.listed_versions = {"1.2.0", "1.3.0"};
        } else {
            rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        }
        if (with_store) {
            CatalogEntry& s = rig.host.add_app(store_app("0.2.0", 77));
            s.listed_versions = {"0.0.1", "0.2.0"};
        }
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine(nullptr, std::move(sources));
        service = rig.make_service();
        service->start_check();
        CancelToken c;
        if (!service->refresh(c, nullptr).ok) throw std::runtime_error("fixture: refresh failed");
        return true;
    }
};

FakeReader installed_testapp_and_store() {
    FakeReader base = with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0"));
    return with_installed(base, store_app("0.0.1"));
}

}  // namespace

PB_TEST(the_preview_lists_the_apps_in_run_order_with_pb_store_last_both_versions_the_sizes_and_their_total) {
    Rig r(installed_testapp_and_store());
    if (!r.prepare(true, true)) PB_SKIP("openssl CLI not available");
    const UpdateAllPreview p = r.service->update_all_preview();
    PB_CHECK(p.ok);
    PB_CHECK_EQ(p.entries.size(), size_t(2));
    PB_CHECK_EQ(p.entries[0].id, std::string("testapp"));
    PB_CHECK_EQ(p.entries[0].installed_version, std::string("1.2.0"));
    PB_CHECK_EQ(p.entries[0].offered_version, std::string("1.3.0"));
    PB_CHECK(!p.entries[0].is_store);
    PB_CHECK_EQ(p.entries[1].id, std::string("pbstore"));                   // PB Store comes last
    PB_CHECK_EQ(p.entries[1].installed_version, std::string("0.0.1"));
    PB_CHECK_EQ(p.entries[1].offered_version, std::string("0.2.0"));
    PB_CHECK(p.entries[1].is_store);
    PB_CHECK(p.includes_store);
    PB_CHECK(p.entries[0].package_size > 0 && p.entries[1].package_size > 0);   // from the signed catalog
    PB_CHECK_EQ(p.total_bytes, p.entries[0].package_size + p.entries[1].package_size);
    const std::vector<std::string> queue = r.service->update_all_queue();
    PB_CHECK_EQ(queue.size(), p.entries.size());                            // the same apps in the same order as the queue
    for (size_t i = 0; i < queue.size(); ++i) PB_CHECK_EQ(queue[i], p.entries[i].id);
}

PB_TEST(entries_is_empty_exactly_when_canUpdateAll_is_false_here_when_update_all_queue_is_empty) {
    Rig r(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));   // current: 1.2.0 installed, 1.2.0 offered
    if (!r.prepare(false, false)) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.service->update_all_queue().empty());
    const UpdateAllPreview p = r.service->update_all_preview();
    PB_CHECK(p.entries.empty());
    PB_CHECK_EQ(p.total_bytes, int64_t(0));
    PB_CHECK(!p.includes_store);
}

PB_TEST(reader_consents_are_unknown_model_and_unknown_firmware_when_the_reader_cannot_be_identified) {
    Rig r(installed_testapp_and_store());
    if (!r.prepare(true, true)) PB_SKIP("openssl CLI not available");       // no identity source: both are unknown
    const UpdateAllPreview p = r.service->update_all_preview();
    std::vector<std::string> names;
    for (const ConfirmationItem& c : p.reader_consents) {
        names.push_back(c.name);
        PB_CHECK(!c.label.empty());
    }
    std::sort(names.begin(), names.end());
    PB_CHECK((names == std::vector<std::string>{"unknown-firmware", "unknown-model"}));
}

PB_TEST(a_readable_inkpad_one_identity_needs_no_reader_consent) {
    FixedIdentity known("PB1030", "6.11.1437");   // as the device wrapper hands it over: the "U1030." prefix is already stripped
    Rig r(installed_testapp_and_store());
    if (!r.prepare(true, true, {&known})) PB_SKIP("openssl CLI not available");
    const UpdateAllPreview p = r.service->update_all_preview();
    PB_CHECK(p.ok);
    PB_CHECK(p.reader_consents.empty());
}

PB_TEST(a_firmware_string_the_engine_cannot_parse_still_needs_unknown_firmware_once_and_nothing_else) {
    FixedIdentity garbled("PB1030", "garbage");
    Rig r(installed_testapp_and_store());
    if (!r.prepare(true, true, {&garbled})) PB_SKIP("openssl CLI not available");
    const UpdateAllPreview p = r.service->update_all_preview();
    PB_CHECK_EQ(p.reader_consents.size(), size_t(1));
    PB_CHECK_EQ(p.reader_consents[0].name, std::string("unknown-firmware"));
}

PB_TEST(the_battery_risk_is_reported_only_when_the_level_cannot_be_read) {
    Rig r(installed_testapp_and_store());
    if (!r.prepare(true, true)) PB_SKIP("openssl CLI not available");
    PB_CHECK(!r.service->update_all_preview().battery_unreadable);
    r.rig.battery.set_percent_unreadable();
    PB_CHECK(r.service->update_all_preview().battery_unreadable);
}

PB_TEST(the_preview_makes_no_network_request_and_writes_nothing_to_the_reader) {
    Rig r(installed_testapp_and_store());
    if (!r.prepare(true, true)) PB_SKIP("openssl CLI not available");
    r.rig.radio = FakeRadio();                                               // forget the radio calls of the refresh
    const size_t requests = r.rig.requests().size();
    const Snapshot before = snapshot(r.rig.root());
    const UpdateAllPreview p = r.service->update_all_preview();
    PB_CHECK(p.ok && !p.entries.empty());
    PB_CHECK_EQ(r.rig.requests().size(), requests);                         // no request to the catalog host
    PB_CHECK_EQ(r.rig.radio.count("connect"), 0);                            // the radio was not touched
    PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);
}

PB_TEST(while_a_repair_is_pending_the_preview_is_not_ok_and_has_no_entries) {
    // an update of testapp cut by a lost power at some write leaves a journal; the next start has a repair pending
    for (int cut = 10; cut < 70; ++cut) {
        Rig r(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));
        if (!r.prepare(true, false)) PB_SKIP("openssl CLI not available");
        const ReaderFsFactory normal = r.rig.storage.open_fs;
        r.rig.storage.open_fs = [cut, normal](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<ThrowingFs>(normal(root), cut); };
        r.rig.make_engine();
        r.service = r.rig.make_service();
        r.service->start_check();
        PlanView plan = r.service->plan_action(RowAction::Update, "testapp", r.cancel, nullptr);
        if (!plan.ok) throw std::runtime_error("fixture: no plan");
        std::vector<std::string> given;
        for (const ConfirmationItem& i : plan.confirmations) given.push_back(i.name);
        bool lost = false;
        try { r.service->execute_plan(given, false, r.cancel, nullptr); } catch (const PowerLoss&) { lost = true; }
        r.rig.storage.open_fs = normal;
        if (!lost) PB_FAIL_AT("no cut left a journal");
        r.rig.make_engine();
        r.service = r.rig.make_service();
        if (!r.service->start_check().repair_pending) continue;
        const UpdateAllPreview p = r.service->update_all_preview();
        PB_CHECK(!p.ok);
        PB_CHECK(p.entries.empty());
        PB_CHECK(has_rule(p.findings, "E-INS-23"));
        return;
    }
    PB_FAIL_AT("no cut left a repair pending");
}

PB_TEST_MAIN()
