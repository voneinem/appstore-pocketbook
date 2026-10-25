// T078: deleting a data folder is separate from the removal and needs two different confirmations.
#include "install_fixtures.h"
#include "pbstore/install/delete_data.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

Identity known() { return Identity{std::string("1030"), std::string("6.11.1437")}; }

FakeReader reader_with_data() {
    FakeReader r;
    r.add_data_dir("testapp", {{"notes.db", "12345"}, {"sub/b.db", "123"}, {"sub/deeper/c.db", "1"}});
    return r;
}

DeleteDataPlanResult plan_for(ReaderFs& fs, const std::string& app, const std::optional<std::string>& dir) {
    ScanRequest req;
    req.app_id = app;
    if (dir) req.data_dir = *dir;
    return plan_delete_data(app, dir, scan(fs, known(), req));
}

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
}  // namespace

PB_TEST(the_plan_names_the_folder_the_file_count_and_the_size_and_needs_both_confirmations) {
    FakeReader r = reader_with_data();
    Volume v(r);
    DeleteDataPlanResult res = plan_for(v.fat, "testapp", std::string("system/testapp"));
    PB_CHECK(res.plan.has_value());
    const Plan& p = *res.plan;
    PB_CHECK(p.kind == PlanKind::DeleteData);
    PB_CHECK_EQ(p.data->path, std::string("system/testapp"));
    PB_CHECK_EQ(p.data->files, uint64_t(3));
    PB_CHECK_EQ(p.data->bytes, uint64_t(9));
    PB_CHECK(p.confirmations == (Confirmations{Confirmation::DeleteData1, Confirmation::DeleteData2}));
    PB_CHECK(p.items.empty());
    // the two questions differ in wording and in the token they ask for
    const std::string q1 = delete_data_question_1(p), q2 = delete_data_question_2(p);
    PB_CHECK(q1 != q2);
    PB_CHECK(q1.find("system/testapp") != std::string::npos);
    PB_CHECK(q2.find("3") != std::string::npos);
    PB_CHECK_EQ(delete_data_token_1(p), std::string("system/testapp"));
    PB_CHECK_EQ(delete_data_token_2(p), std::string("3"));
    PB_CHECK(delete_data_token_1(p) != delete_data_token_2(p));
    // only the folder and its size are in the text; no file names, nothing about the contents
    const std::string text = render_plan(p);
    PB_CHECK(text.find("notes.db") == std::string::npos);
    PB_CHECK(text.find("3 files") != std::string::npos);
}

PB_TEST(with_both_tokens_the_folder_is_deleted_bottom_up_and_nothing_else_changes) {
    FakeReader r = reader_with_data();
    Volume v(r);
    Snapshot before = snapshot(v.fat);
    DeleteDataPlanResult res = plan_for(v.fat, "testapp", std::string("system/testapp"));
    CancelToken cancel;
    DeleteResult d = delete_data(v.fat, *res.plan, "system/testapp", "3", nullptr, cancel);
    PB_CHECK(d.done);
    PB_CHECK_EQ(d.files_deleted, uint64_t(3));
    PB_CHECK(!v.fat.stat("system/testapp").exists());
    Diff diff = compare(before, snapshot(v.fat));
    for (const std::string& p : diff.removed) PB_CHECK(p.compare(0, 14, "system/testapp") == 0);
    PB_CHECK(diff.added.empty() && diff.changed.empty());
}

PB_TEST(a_wrong_or_missing_token_at_either_step_keeps_the_folder) {
    FakeReader r = reader_with_data();
    Volume v(r);
    Snapshot before = snapshot(v.fat);
    DeleteDataPlanResult res = plan_for(v.fat, "testapp", std::string("system/testapp"));
    CancelToken cancel;
    for (auto tokens : {std::make_pair("system/testapp", "4"), std::make_pair("system/other", "3"), std::make_pair("", "3"), std::make_pair("system/testapp", ""),
                        std::make_pair("system/testapp/", "3"), std::make_pair("SYSTEM/TESTAPP", "3"), std::make_pair("3", "system/testapp")}) {
        DeleteResult d = delete_data(v.fat, *res.plan, tokens.first, tokens.second, nullptr, cancel);
        PB_CHECK(!d.done);
        PB_CHECK(d.wrong_token_1 || d.wrong_token_2);
        PB_CHECK(compare(before, snapshot(v.fat)).identical);
    }
}

PB_TEST(links_are_never_followed) {
    FakeReader r = reader_with_data();
    Volume v(r);
    PB_CHECK(v.mem.make_dir("outside"));
    PB_CHECK(v.mem.create_exclusive("outside/keep.txt", "not in the data folder", true));
    PB_CHECK(v.mem.plant_link("system/testapp/link-to-outside"));
    DeleteDataPlanResult res = plan_for(v.fat, "testapp", std::string("system/testapp"));
    PB_CHECK(res.plan.has_value());
    CancelToken cancel;
    DeleteResult d = delete_data(v.fat, *res.plan, "system/testapp", std::to_string(res.plan->data->files), nullptr, cancel);
    PB_CHECK(d.done);
    PB_CHECK(!v.fat.stat("system/testapp").exists());
    std::string c;
    PB_CHECK(v.fat.read_file("outside/keep.txt", 100, &c));      // what the link pointed to is untouched
    // and a data folder that itself is a link is refused
    Volume w(r);
    w.mem.plant_link("system/linked");
    DeleteDataPlanResult bad = plan_for(w.fat, "testapp", std::string("system/linked"));
    PB_CHECK(!bad.plan.has_value());
    PB_CHECK(has_rule(bad.findings, "E-INS-22"));
}

PB_TEST(a_folder_shared_with_another_app_or_no_folder_at_all_is_not_deleted) {
    FakeReader r;
    r = with_installed(r, nested_sample_app("otherapp", "1.0.0", "U_OtherApp"));
    r.add_data_dir("otherapp", {{"x.db", "1"}});
    Volume v(r);
    DeleteDataPlanResult shared = plan_for(v.fat, "testapp", std::string("system/otherapp"));
    PB_CHECK(!shared.plan.has_value());
    PB_CHECK(has_rule(shared.findings, "E-INS-16"));
    DeleteDataPlanResult none = plan_for(v.fat, "testapp", std::nullopt);
    PB_CHECK(!none.plan.has_value());
    PB_CHECK(none.findings.empty());
    PB_CHECK(!none.notices.empty());
    DeleteDataPlanResult missing = plan_for(v.fat, "testapp", std::string("system/never-existed"));
    PB_CHECK(!missing.plan.has_value());
    PB_CHECK(missing.findings.empty());
    DeleteDataPlanResult reserved = plan_for(v.fat, "testapp", std::string("system/config"));
    PB_CHECK(!reserved.plan.has_value());
    PB_CHECK(has_rule(reserved.findings, "E-INS-18"));
    DeleteDataPlanResult outside = plan_for(v.fat, "testapp", std::string("applications/alpha.app"));
    PB_CHECK(!outside.plan.has_value());
    PB_CHECK(!outside.findings.empty());
}

PB_TEST(an_interruption_leaves_a_partly_deleted_folder_and_running_the_step_again_finishes_it) {
    FakeReader r = reader_with_data();
    int total = 0;
    {
        Volume v(r);
        DeleteDataPlanResult res = plan_for(v.fat, "testapp", std::string("system/testapp"));
        v.mem.reboot();
        CancelToken cancel;
        PB_CHECK(delete_data(v.fat, *res.plan, "system/testapp", "3", nullptr, cancel).done);
        total = v.mem.mutating_calls();
    }
    for (int n = 2; n < total; n += 2) {
        Volume v(r);
        DeleteDataPlanResult res = plan_for(v.fat, "testapp", std::string("system/testapp"));
        v.mem.lose_power_at(n, false);
        CancelToken cancel;
        try { delete_data(v.fat, *res.plan, "system/testapp", "3", nullptr, cancel); } catch (const PowerLoss&) {}
        v.mem.reboot();
        // it is not an unfinished installation
        PB_CHECK(!plan_repair(v.fat).plan.has_value());
        PB_CHECK(!journal_exists(v.fat));
        // the step again finishes it
        DeleteDataPlanResult again = plan_for(v.fat, "testapp", std::string("system/testapp"));
        if (!again.plan) continue;   // it had already gone completely
        CancelToken c2;
        PB_CHECK(delete_data(v.fat, *again.plan, "system/testapp", std::to_string(again.plan->data->files), nullptr, c2).done);
        PB_CHECK(!v.fat.stat("system/testapp").exists());
    }
}

PB_TEST(a_cancel_stops_the_deletion_between_files) {
    FakeReader r = reader_with_data();
    Volume v(r);
    DeleteDataPlanResult res = plan_for(v.fat, "testapp", std::string("system/testapp"));
    CancelToken cancel;
    int events = 0;
    DeleteResult d = delete_data(v.fat, *res.plan, "system/testapp", "3", [&](const Progress&) { if (++events == 2) cancel.cancel(); }, cancel);
    PB_CHECK(!d.done);
    PB_CHECK(d.cancelled);
    PB_CHECK(v.fat.stat("system/testapp").exists());          // partly deleted; the step can be run again
}

PB_TEST(a_changed_folder_since_the_plan_is_refused) {
    FakeReader r = reader_with_data();
    Volume v(r);
    DeleteDataPlanResult res = plan_for(v.fat, "testapp", std::string("system/testapp"));
    PB_CHECK(v.fat.create_exclusive("system/testapp/appeared.db", "new", true));
    CancelToken cancel;
    DeleteResult d = delete_data(v.fat, *res.plan, "system/testapp", "3", nullptr, cancel);
    PB_CHECK(!d.done);
    PB_CHECK(has_rule(d.findings, "E-INS-25"));
    PB_CHECK(v.fat.stat("system/testapp/appeared.db").exists());
}

PB_TEST_MAIN()
