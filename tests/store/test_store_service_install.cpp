// T099 (story 2): what the store does when an install cannot go ahead. Each refusal leaves the reader byte-identical, with the right rule id and
// the three parts (what happened, that the reader was not changed, what to do), and Wi-Fi is back where it was. The happy path and the gates
// (battery, memory, consents) are in test_store_service_actions; the engine's own rules are tested with the engine, here the store's front is
// shown not to turn a refusal into a write.
#include <fstream>

#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}

Snapshot reader_only(const Snapshot& s) {   // the reader without the store's own state folder and the engine's lock
    Snapshot out;
    for (const auto& i : s) {
        if (i.path.compare(0, 18, "system/pbstore/app") == 0) continue;
        if (i.path == "system/pbstore/lock") continue;
        out.push_back(i);
    }
    return out;
}

struct Refusal {
    const char* name;
    const char* rule;
    std::function<void(FakeReader&)> change;   // what is wrong with the reader
};

std::vector<std::string> names(const PlanView& p) {
    std::vector<std::string> v;
    for (const ConfirmationItem& i : p.confirmations) v.push_back(i.name);
    return v;
}

}  // namespace

PB_TEST(a_reader_that_cannot_take_the_app_refuses_with_three_parts_and_stays_byte_identical) {
    const std::vector<Refusal> cases = {
        {"the menu group does not exist", "E-INS-14", [](FakeReader& r) { r.groups.erase(r.groups.begin()); r.files[FakeReader::kViewPath] = r.view_json(); }},
        {"the entry id is in use by another app", "E-INS-15", [](FakeReader& r) { r.add_hand_installed(make_sample_app("other", "1.0.0", "U_TestApp", "system/other", 5)); }},
        {"view.json is not valid JSON", "E-INS-12", [](FakeReader& r) { r.files[FakeReader::kViewPath] = "{ this is not json"; }},
        {"view.json lacks the expected parts", "E-INS-13", [](FakeReader& r) { r.files[FakeReader::kViewPath] = "{\"version\": \"8\"}\n"; }},
        {"a file that the installer did not create is in the way", "E-INS-20", [](FakeReader& r) { r.put_file("applications/testapp.bin", "somebody else's file"); }},
    };
    for (const Refusal& c : cases) {
        FakeReader reader;
        c.change(reader);
        StoreRig rig(reader);
        if (!rig.ready()) PB_SKIP("openssl CLI not available");
        rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        auto service = rig.make_service();
        service->start_check();
        CancelToken cancel;
        PB_CHECK(service->refresh(cancel, nullptr).ok);
        rig.radio = FakeRadio();
        const Snapshot before = snapshot(rig.root());
        PlanView plan = service->plan_action(RowAction::Install, "testapp", cancel, nullptr);
        if (plan.ok) {   // some rules are found when the write is checked again, not when the plan is made
            ExecView out = service->execute_plan(names(plan), false, cancel, nullptr);
            PB_CHECK(!out.ok);
            if (!has_rule(out.findings, c.rule)) PB_FAIL_AT(std::string(c.name) + ": expected " + c.rule + " when carrying the plan out");
            PB_CHECK(!out.messages().empty());
        } else {
            if (!has_rule(plan.findings, c.rule)) PB_FAIL_AT(std::string(c.name) + ": expected " + c.rule + ", got " + (plan.findings.empty() ? "nothing" : plan.findings[0].rule_id));
            const Message m = plan.messages()[0];
            PB_CHECK(!m.what.empty() && !m.reader_effect.empty() && !m.next.empty());           // the three parts
            PB_CHECK(m.reader_effect.find("not changed") != std::string::npos);
            PB_CHECK(m.what.find(c.rule) != std::string::npos);                                  // the rule id stays in the first part
        }
        if (!compare(reader_only(before), reader_only(snapshot(rig.root()))).identical) PB_FAIL_AT(std::string(c.name) + ": the reader changed");
        PB_CHECK(rig.radio.is_on() == std::optional<bool>(false));                               // Wi-Fi restored after the download of the plan
    }
}

PB_TEST(a_data_folder_that_another_installed_app_owns_refuses_the_install) {
    FakeReader reader = with_installed(FakeReader(), make_sample_app("other", "1.0.0", "U_Other", "system/testapp", 5));
    StoreRig rig(reader);
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    rig.host.publish(100);
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    service->start_check();
    CancelToken cancel;
    PB_CHECK(service->refresh(cancel, nullptr).ok);
    const Snapshot before = snapshot(rig.root());
    PlanView plan = service->plan_action(RowAction::Install, "testapp", cancel, nullptr);
    PB_CHECK(!plan.ok);
    PB_CHECK(has_rule(plan.findings, "E-INS-16"));
    PB_CHECK(compare(reader_only(before), reader_only(snapshot(rig.root()))).identical);
}

PB_TEST(not_enough_memory_is_refused_before_anything_is_downloaded) {
    // (the early check of the free storage, E-INS-07, reads the real folder's free space here and so cannot be made to fail on a host; the engine's
    // own space tests cover it, and the executor's check just before the first write is the same rule)
    StoreRig rig;
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    rig.host.publish(100);
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    service->start_check();
    CancelToken cancel;
    PB_CHECK(service->refresh(cancel, nullptr).ok);
    rig.radio = FakeRadio();
    rig.memory.set_available(1 << 20);                                                      // 1 MiB: the package needs twice its size and 32 MiB more
    const int attempts = rig.connection_attempts();
    const Snapshot before = snapshot(rig.root());
    PlanView plan = service->plan_action(RowAction::Install, "testapp", cancel, nullptr);
    PB_CHECK(!plan.ok);
    PB_CHECK(has_rule(plan.findings, "E-STO-03"));
    PB_CHECK_EQ(rig.connection_attempts(), attempts);                                       // the package was never requested
    PB_CHECK(rig.radio.calls().empty());                                                    // and Wi-Fi was never switched on for it
    PB_CHECK(compare(reader_only(before), reader_only(snapshot(rig.root()))).identical);
}

PB_TEST(a_download_that_stops_half_way_is_refused_with_the_reader_unchanged_and_wifi_restored) {
    StoreRig rig;
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    rig.host.publish(100);
    rig.write_reader();
    // the catalog comes through; the package stops: no data for the stall time, as when Wi-Fi drops
    Downloader inner = rig.host.downloader("");
    Downloader dropping = [inner](const std::string& url, uint64_t max, long secs, const DownloadProgress& progress) {
        if (url.find(".zip") != std::string::npos) {
            if (progress) progress(1024, 6000);
            FetchResult r;
            r.ok = false;
            r.kind = FetchKind::Timeout;
            r.error = "no data";
            return r;
        }
        return inner(url, max, secs, progress);
    };
    rig.make_engine(dropping);
    auto service = rig.make_service();
    service->start_check();
    CancelToken cancel;
    PB_CHECK(service->refresh(cancel, nullptr).ok);
    rig.radio = FakeRadio();
    const Snapshot before = snapshot(rig.root());
    PlanView plan = service->plan_action(RowAction::Install, "testapp", cancel, nullptr);
    PB_CHECK(!plan.ok);
    PB_CHECK(has_rule(plan.findings, "E-INS-36") || has_rule(plan.findings, "E-INS-26"));
    const Message m = plan.messages()[0];
    PB_CHECK(!m.next.empty() && m.next.find("Wi-Fi") != std::string::npos);                 // the third part says what to do on a reader
    PB_CHECK(m.reader_effect.find("not changed") != std::string::npos);
    PB_CHECK(rig.radio.is_on() == std::optional<bool>(false));
    PB_CHECK(compare(reader_only(before), reader_only(snapshot(rig.root()))).identical);
    // nothing of the failed plan can be carried out
    ExecView stale = service->execute_plan({}, false, cancel, nullptr);
    PB_CHECK(!stale.ok);
    PB_CHECK(compare(reader_only(before), reader_only(snapshot(rig.root()))).identical);
}

PB_TEST_MAIN()
