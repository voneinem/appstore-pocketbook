// T114: the stories through the assistant's HTTP API, with no browser and no display, on the engine with the
// fake reader and the test catalog: same plan text and same reader contents as the command line.
#include "package_fixtures.h"
#include "test_support.h"
#include "ui_rig.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

namespace J = pbstore::json;

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

J::Value obj() { return J::Value::make_object(); }
J::Value str(const std::string& s) { return J::Value::from_string(s); }
J::Value strings(const std::vector<std::string>& v) {
    J::Value a = J::Value::make_array();
    for (const auto& s : v) a.push(str(s));
    return a;
}
J::Value all_confirmations(const J::Value& plan_answer) {
    J::Value a = J::Value::make_array();
    if (const J::Value* c = plan_answer.find("confirmations"))
        for (const auto& e : c->array) a.push(e);
    return a;
}
bool message_has_rule(const HttpReply& r, const std::string& id) {
    J::Value v = r.json();
    const J::Value* m = v.find("message");
    const J::Value* ids = m ? m->find("rule_ids") : nullptr;
    if (ids)
        for (const auto& e : ids->array)
            if (e.string == id) return true;
    return false;
}
const J::Value* find_app(const J::Value& answer, const std::string& id) {
    const J::Value* apps = answer.find("apps");
    if (apps)
        for (const auto& a : apps->array)
            if (a.get_string("id") == id) return &a;
    return nullptr;
}

struct Ui : UiRig {
    explicit Ui(const FakeReader& r = FakeReader()) : UiRig(r) {}
    bool open() {
        J::Value body = obj();
        body.set("folder", str(root()));
        return client().post("api/reader/open", body).status == 200;
    }
    HttpReply plan(const std::string& action, const std::string& app_id, bool delete_data = false) {
        J::Value body = obj();
        body.set("action", str(action));
        body.set("app_id", str(app_id));
        if (delete_data) body.set("delete_data", J::Value::from_bool(true));
        return client().post("api/plan", body);
    }
    HttpReply execute(const J::Value& plan_answer, const J::Value& confirmations) {
        J::Value body = obj();
        body.set("plan_id", str(plan_answer.get_string("plan_id").value_or("")));
        body.set("confirmations", confirmations);
        return client().post("api/execute", body);
    }
};

}  // namespace

PB_TEST(readers_are_listed_and_with_two_qualifying_drives_the_page_must_choose) {
    Ui u;
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    FakeReader().write_to(u.rig.dir.path() + "/second");
    u.rig.mounts.mount(u.rig.dir.path() + "/second", "vfat", 1ull << 30);
    u.start();
    UiClient c = u.client();
    HttpReply r = c.get("api/readers");
    PB_CHECK_EQ(r.status, 200);
    J::Value v = r.json();
    const J::Value* cands = v.find("candidates");
    PB_CHECK(cands && cands->array.size() == 2);
    for (const auto& cand : cands->array) {
        PB_CHECK(cand.get_bool("qualifies").value_or(false));
        PB_CHECK(!cand.get_string("label").value_or("").empty());
        PB_CHECK(!cand.get_string("mount_point").value_or("").empty());
        PB_CHECK(cand.find("size_text") != nullptr);
    }
    // the assistant never picks one for the user
    HttpReply none = c.post("api/reader/open", "{}");
    PB_CHECK_EQ(none.status, 422);
    PB_CHECK(message_has_rule(none, "E-INS-02"));
    // a candidate chosen by its mount point, and a folder chosen by hand
    J::Value body = obj();
    body.set("folder", str(u.root()));
    HttpReply ok = c.post("api/reader/open", body);
    PB_CHECK_EQ(ok.status, 200);
    PB_CHECK(ok.json().find("reader") != nullptr);
    J::Value wrong = obj();
    wrong.set("folder", str(u.rig.dir.path()));
    HttpReply bad = c.post("api/reader/open", wrong);
    PB_CHECK_EQ(bad.status, 422);
    PB_CHECK(message_has_rule(bad, "E-INS-03"));
}

PB_TEST(the_folder_list_shows_directories_only_below_the_allowed_places) {
    Ui u;
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    ::mkdir((u.rig.dir.path() + "/home").c_str(), 0755);
    ::mkdir((u.rig.dir.path() + "/home/docs").c_str(), 0755);
    u.rig.dir.write("home/a-file.txt", "x");
    u.start();
    UiClient c = u.client();
    HttpReply r = c.get("api/folders?path=" + u.rig.dir.path() + "/home");
    PB_CHECK_EQ(r.status, 200);
    J::Value v = r.json();
    const J::Value* folders = v.find("folders");
    PB_CHECK(folders != nullptr);
    bool docs = false;
    for (const auto& f : folders->array) {
        PB_CHECK(f.get_string("name").value_or("") != "a-file.txt");
        if (f.get_string("name") == std::string("docs")) docs = true;
    }
    PB_CHECK(docs);
    PB_CHECK_EQ(c.get("api/folders?path=/etc").status, 422);
    PB_CHECK_EQ(c.get("api/folders?path=" + u.rig.dir.path() + "/home/../..").status, 422);
}

PB_TEST(installed_and_list_show_the_states_and_the_actions) {
    FakeReader r = with_installed(FakeReader(), nested_sample_app("installedapp", "1.0.0", "U_InstalledApp"));
    r.add_hand_installed(make_sample_app("byhand", "1.0.0", "U_ByHand", "system/byhand"));
    Ui u(r);
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    u.rig.host.add_app(nested_sample_app("installedapp", "1.1.0", "U_InstalledApp"));
    u.rig.host.add_app(nested_sample_app("freshapp", "2.0.0", "U_FreshApp"));
    u.rig.host.add_app(nested_sample_app("byhand", "1.0.0", "U_ByHand"));
    u.start();
    PB_CHECK(u.open());
    UiClient c = u.client();
    J::Value installed = c.get("api/installed").json();
    PB_CHECK(find_app(installed, "installedapp") != nullptr);
    PB_CHECK(find_app(installed, "freshapp") == nullptr);                     // not on the reader
    J::Value list = c.post("api/list", "{}").json();
    PB_CHECK(!list.get_string("catalog_generated_at").value_or("").empty());
    const J::Value* fresh = find_app(list, "freshapp");
    const J::Value* inst = find_app(list, "installedapp");
    const J::Value* hand = find_app(list, "byhand");
    PB_CHECK(fresh && inst && hand);
    PB_CHECK_EQ(fresh->get_string("state").value_or(""), std::string("not-installed"));
    PB_CHECK_EQ(fresh->get_string("state_text").value_or(""), std::string("not installed"));
    PB_CHECK_EQ(inst->get_string("state").value_or(""), std::string("installed"));
    PB_CHECK(inst->get_bool("update_available").value_or(false));
    PB_CHECK_EQ(hand->get_string("state").value_or(""), std::string("outside-installer"));
    PB_CHECK_EQ(hand->get_string("state_text").value_or(""), std::string("installed outside the installer"));
    auto actions = [](const J::Value* a) {
        std::vector<std::string> v;
        for (const auto& e : a->find("actions")->array) v.push_back(e.string);
        return v;
    };
    PB_CHECK_EQ(actions(fresh), (std::vector<std::string>{"install"}));
    PB_CHECK_EQ(actions(inst), (std::vector<std::string>{"update", "remove"}));
    PB_CHECK_EQ(actions(hand), (std::vector<std::string>{"adopt", "replace"}));
    PB_CHECK(fresh->get_string("description").has_value());
}

PB_TEST(a_preview_cannot_be_executed_and_a_missing_confirmation_is_named_and_enforced_by_the_engine) {
    Ui u;
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    u.rig.host.add_app(nested_sample_app());
    u.start();
    PB_CHECK(u.open());
    Snapshot before = snapshot(u.root());
    UiClient c = u.client();
    J::Value body = obj();
    body.set("action", str("install"));
    body.set("app_id", str("testapp"));
    HttpReply preview = c.post("api/preview", body);
    PB_CHECK_EQ(preview.status, 200);
    J::Value pv = preview.json();
    PB_CHECK(pv.get_bool("dry_run_only").value_or(false));
    HttpReply refused = u.execute(pv, all_confirmations(pv));
    PB_CHECK_EQ(refused.status, 422);
    PB_CHECK(compare(before, snapshot(u.root())).identical);

    HttpReply planned = u.plan("install", "testapp");
    PB_CHECK_EQ(planned.status, 200);
    J::Value p = planned.json();
    PB_CHECK(!p.get_bool("dry_run_only").value_or(true));
    PB_CHECK(has(p.get_string("text").value_or(""), "U_TestApp"));
    // the page claims only the plan confirmation: the engine names what is missing
    J::Value only_plan = J::Value::make_array();
    only_plan.push(str("plan"));
    HttpReply missing = u.execute(p, only_plan);
    PB_CHECK_EQ(missing.status, 422);
    J::Value mv = missing.json();
    const J::Value* names = mv.find("missing");
    PB_CHECK(names && names->array.size() == 2);
    std::vector<std::string> got;
    for (const auto& n : names->array) got.push_back(n.string);
    std::sort(got.begin(), got.end());
    PB_CHECK_EQ(got, (std::vector<std::string>{"unknown-firmware", "unknown-model"}));
    PB_CHECK(compare(before, snapshot(u.root())).identical);
    // an empty set of confirmations and one made up
    PB_CHECK_EQ(u.execute(p, J::Value::make_array()).status, 422);
    PB_CHECK_EQ(u.execute(p, strings({"yes-to-all"})).status, 422);
    PB_CHECK(compare(before, snapshot(u.root())).identical);
    // with every confirmation the plan names
    HttpReply done = u.execute(p, all_confirmations(p));
    PB_CHECK_EQ(done.status, 200);
    PB_CHECK(done.json().get_bool("done").value_or(false));
    const std::string final_text = done.json().get_string("text").value_or("");
    PB_CHECK(has(final_text, "eject"));
    PB_CHECK(has(final_text, "restart"));
    PB_CHECK(has(final_text, "take noticeably longer"));   // observed on a real reader: the first start after a change is slower
    PB_CHECK(has(final_text, "@General"));
    PB_CHECK(std::ifstream(u.root() + "/applications/testapp.app").good());
    // a plan can be executed once
    PB_CHECK_EQ(u.execute(p, all_confirmations(p)).status, 422);
}

PB_TEST(the_plan_and_the_reader_contents_equal_those_of_the_command_line) {
    // the same reader, the same catalog, the same inputs on both front ends; the clock stands still in both
    Ui u;
    CliRig cli;
    if (!u.ready() || !cli.ready()) PB_SKIP("openssl CLI or the test program is not available");
    u.rig.host.add_app(nested_sample_app());
    u.rig.clock.set_utc(2026, 10, 25, 12, 0, 0);
    u.rig.clock.set_local(2026, 10, 25, 14, 0, 0, 120);
    u.start();
    PB_CHECK(u.open());
    cli.host.add_app(nested_sample_app());
    cli.host.publish(100);
    ::mkdir((cli.dir.path() + "/home").c_str(), 0755);
    cli.write_reader();

    HttpReply planned = u.plan("install", "testapp");
    J::Value p = planned.json();
    const std::string ui_text = p.get_string("text").value_or("");
    RunResult dry = cli.run({"install", "testapp", "--reader", cli.root(), "--dry-run", "--confirm-unknown-model", "--confirm-unknown-firmware"}, {"PBSTORE_TEST_NOW=1"});
    PB_CHECK_EQ(dry.code, 0);
    if (!has(dry.out, ui_text)) PB_FAIL_AT("the plan texts differ:\n--- ui ---\n" + ui_text + "\n--- cli ---\n" + dry.out);   // exactly the same plan text
    HttpReply done = u.execute(p, all_confirmations(p));
    PB_CHECK_EQ(done.status, 200);
    RunResult real = cli.run({"install", "testapp", "--reader", cli.root(), "--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"}, {"PBSTORE_TEST_NOW=1"});
    PB_CHECK_EQ(real.code, 0);
    PB_CHECK(has(real.out, done.json().get_string("text").value_or("?")));  // the same final message
    Diff d = compare(snapshot(cli.root()), snapshot(u.root()));
    if (!d.identical) PB_FAIL_AT("the reader contents differ between the two front ends: " + d.describe());
}

PB_TEST(every_story_gives_the_same_reader_contents_through_the_assistant_and_through_the_command_line) {
    struct Story {
        const char* name;
        std::function<FakeReader()> reader;
        std::function<void(CatalogHost&)> catalog;
        std::string action;                    // the assistant's action
        std::vector<std::string> cli;          // the command line
    };
    const std::vector<std::string> confirm = {"--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"};
    auto with = [&](std::vector<std::string> a, const std::vector<std::string>& more) { a.insert(a.end(), more.begin(), more.end()); return a; };
    std::vector<Story> stories = {
        {"remove", [] { return with_installed(FakeReader(), nested_sample_app()); }, [](CatalogHost& h) { h.add_app(nested_sample_app()); }, "remove", {"remove", "testapp"}},
        {"update", [] { return with_installed(FakeReader(), nested_sample_app()); }, [](CatalogHost& h) { h.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0")); }, "update", {"update", "testapp"}},
        {"adopt", [] { FakeReader f; f.add_hand_installed(nested_sample_app()); return f; }, [](CatalogHost& h) { h.add_app(nested_sample_app()); }, "adopt", {"adopt", "testapp"}},
        {"replace", [] { FakeReader f; f.add_hand_installed(nested_sample_app("testapp", "1.0.0", "U_TestApp", 2)); return f; }, [](CatalogHost& h) { h.add_app(nested_sample_app()); }, "replace", {"replace", "testapp"}},
    };
    for (Story& st : stories) {
        Ui u(st.reader());
        CliRig cli(st.reader());
        if (!u.ready() || !cli.ready()) PB_SKIP("openssl CLI or the test program is not available");
        st.catalog(u.rig.host);
        st.catalog(cli.host);
        u.start();
        PB_CHECK(u.open());
        cli.host.publish(100);
        ::mkdir((cli.dir.path() + "/home").c_str(), 0755);
        cli.write_reader();
        J::Value p = u.plan(st.action, "testapp").json();
        const std::string ui_text = p.get_string("text").value_or("");
        std::vector<std::string> args = with(st.cli, {"--reader", cli.root()});
        std::vector<std::string> extra = confirm;
        extra.push_back("--confirm-replace");
        RunResult real = cli.run(with(args, extra), {"PBSTORE_TEST_NOW=1"});
        if (real.code != 0) PB_FAIL_AT(std::string(st.name) + ": the command line ended with " + std::to_string(real.code) + "\n" + real.err);
        if (!has(real.out, ui_text)) PB_FAIL_AT(std::string(st.name) + ": the plan texts differ\n--- ui ---\n" + ui_text + "\n--- cli ---\n" + real.out);
        HttpReply done = u.execute(p, all_confirmations(p));
        if (done.status != 200) PB_FAIL_AT(std::string(st.name) + ": the assistant refused: " + done.body);
        Diff d = compare(snapshot(cli.root()), snapshot(u.root()));
        if (!d.identical) PB_FAIL_AT(std::string(st.name) + ": the reader contents differ: " + d.describe());
        PB_CHECK(has(real.out, done.json().get_string("text").value_or("?")));
    }
}

PB_TEST(remove_with_the_data_folder_asks_for_two_different_tokens_and_the_engine_checks_them) {
    FakeReader f;
    f.add_data_dir("testapp", {{"a.db", "12345"}, {"b.db", "123"}});
    Ui u(with_installed(f, nested_sample_app()));
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    u.rig.host.add_app(nested_sample_app());
    u.start();
    PB_CHECK(u.open());
    HttpReply planned = u.plan("remove", "testapp", /*delete_data=*/true);
    PB_CHECK_EQ(planned.status, 200);
    J::Value p = planned.json();
    PB_CHECK(has(p.get_string("text").value_or(""), "two further confirmations"));
    HttpReply removed = u.execute(p, all_confirmations(p));
    PB_CHECK_EQ(removed.status, 200);
    J::Value rv = removed.json();
    const J::Value* deletion = rv.find("data_deletion");
    PB_CHECK(deletion != nullptr);
    PB_CHECK_EQ(deletion->get_string("folder").value_or(""), std::string("system/testapp"));
    PB_CHECK_EQ(deletion->get_int("files").value_or(0), int64_t(2));
    PB_CHECK(deletion->get_string("question_1").value_or("") != deletion->get_string("question_2").value_or(""));
    PB_CHECK(std::ifstream(u.root() + "/system/testapp/a.db").good());       // the app is gone; the data folder is still there
    auto delete_with = [&](const std::string& folder, const std::string& count, const std::vector<std::string>& confirmations) {
        J::Value body = obj();
        body.set("plan_id", str(deletion->get_string("plan_id").value()));
        body.set("confirmations", strings(confirmations));
        J::Value tokens = obj();
        tokens.set("folder", str(folder));
        tokens.set("count", str(count));
        body.set("tokens", tokens);
        return u.client().post("api/execute", body);
    };
    PB_CHECK_EQ(delete_with("system/testapp", "2", {"delete-data-1"}).status, 422);                 // the second one is missing
    PB_CHECK_EQ(delete_with("system/testapp", "9", {"delete-data-1", "delete-data-2"}).status, 422);   // a wrong number
    PB_CHECK_EQ(delete_with("system/other", "2", {"delete-data-1", "delete-data-2"}).status, 422);     // a wrong folder
    PB_CHECK(std::ifstream(u.root() + "/system/testapp/a.db").good());
    HttpReply ok = delete_with("system/testapp", "2", {"delete-data-1", "delete-data-2"});
    PB_CHECK_EQ(ok.status, 200);
    PB_CHECK(!std::ifstream(u.root() + "/system/testapp/a.db").good());
}

PB_TEST(update_adopt_replace_and_a_local_file_work_through_the_api) {
    {   // update
        Ui u(with_installed(FakeReader(), nested_sample_app()));
        if (!u.ready()) PB_SKIP("openssl CLI not available");
        u.rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
        u.start();
        PB_CHECK(u.open());
        J::Value p = u.plan("update", "testapp").json();
        PB_CHECK(has(p.get_string("text").value_or(""), "1.2.0 -> 1.3.0"));
        PB_CHECK_EQ(u.execute(p, all_confirmations(p)).status, 200);
        PB_CHECK(std::ifstream(u.root() + "/applications/testapp/new.bin").good());
    }
    {   // adopt, and replace of an app whose file differs
        FakeReader hand;
        hand.add_hand_installed(nested_sample_app());
        hand.files["applications/testapp.bin"][2] ^= 1;
        Ui u(hand);
        if (!u.ready()) PB_SKIP("openssl CLI not available");
        u.rig.host.add_app(nested_sample_app());
        u.start();
        PB_CHECK(u.open());
        HttpReply adopt = u.plan("adopt", "testapp");
        PB_CHECK_EQ(adopt.status, 422);
        PB_CHECK(message_has_rule(adopt, "E-INS-20"));
        J::Value p = u.plan("replace", "testapp").json();
        J::Value cs = all_confirmations(p);
        std::vector<std::string> names;
        for (const auto& c : cs.array) names.push_back(c.string);
        PB_CHECK(std::find(names.begin(), names.end(), "replace") != names.end());
        // the replace confirmation is missing: refused, naming it
        std::vector<std::string> without;
        for (const auto& n : names) if (n != "replace") without.push_back(n);
        HttpReply refused = u.execute(p, strings(without));
        PB_CHECK_EQ(refused.status, 422);
        PB_CHECK(has(refused.body, "replace"));
        PB_CHECK_EQ(u.execute(p, cs).status, 200);
    }
    {   // a local file, with the catalog unreachable
        Ui u;
        if (!u.ready()) PB_SKIP("openssl CLI not available");
        u.start([](const std::string&, uint64_t, long, const DownloadProgress&) {
            FetchResult r;
            r.kind = FetchKind::Unreachable;
            r.error = "cannot connect";
            return r;
        });
        PB_CHECK(u.open());
        u.rig.dir.write("local.zip", build_package(nested_sample_app()));
        J::Value body = obj();
        body.set("action", str("install-local"));
        body.set("local_file", str(u.rig.dir.file("local.zip")));
        HttpReply planned = u.client().post("api/plan", body);
        PB_CHECK_EQ(planned.status, 200);
        J::Value p = planned.json();
        PB_CHECK(p.get_string("text").value_or("").compare(0, 6, "NOTE: ") == 0);
        PB_CHECK(has(p.get_string("text").value_or(""), "not from the catalog, not signed by the maintainer"));
        PB_CHECK_EQ(u.execute(p, all_confirmations(p)).status, 200);
        J::Value nofile = obj();
        nofile.set("action", str("install-local"));
        nofile.set("local_file", str(u.rig.dir.file("missing.zip")));
        HttpReply bad = u.client().post("api/plan", nofile);
        PB_CHECK_EQ(bad.status, 422);
        PB_CHECK(message_has_rule(bad, "E-INS-27"));
    }
}

PB_TEST(an_interrupted_operation_is_shown_refused_for_new_plans_and_repaired_on_request) {
    // cut an installation off with the in-memory volume, then give its content to the real directory
    FakeReader base;
    CrashFs mem;
    base.load_into(mem);
    FatRulesFs fat(mem);
    Built b = make_install_plan(fat, nested_sample_app());
    mem.lose_power_at(30, false);
    try { run_execute(fat, b); } catch (const PowerLoss&) {}
    mem.reboot();
    FakeReader cut = base;
    cut.assign_tree(mem.tree());
    Ui u(cut);
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    u.rig.host.add_app(nested_sample_app("otherapp", "1.0.0", "U_OtherApp"));
    u.start();
    PB_CHECK(u.open());
    UiClient c = u.client();
    J::Value installed = c.get("api/installed").json();
    const J::Value* interrupted = installed.find("interrupted");
    PB_CHECK(interrupted != nullptr && !interrupted->is_null());
    PB_CHECK(has(interrupted->get_string("text").value_or(""), "testapp"));
    HttpReply refused = u.plan("install", "otherapp");
    PB_CHECK_EQ(refused.status, 422);
    PB_CHECK(message_has_rule(refused, "E-INS-23"));
    J::Value rp = c.post("api/repair/plan", "{}").json();
    PB_CHECK(rp.get_bool("interrupted").value_or(false));
    PB_CHECK(!rp.get_string("text").value_or("").empty());
    HttpReply no = c.post("api/repair", "{\"confirmations\":[]}");
    PB_CHECK_EQ(no.status, 422);
    PB_CHECK(has(no.body, "repair"));
    HttpReply yes = c.post("api/repair", "{\"confirmations\":[\"repair\"]}");
    PB_CHECK_EQ(yes.status, 200);
    PB_CHECK(yes.json().get_bool("done").value_or(false));
    PB_CHECK(!c.post("api/repair/plan", "{}").json().get_bool("interrupted").value_or(true));
}

PB_TEST(a_second_request_while_an_operation_runs_answers_409_and_cancel_ends_the_first) {
    Ui u;
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    u.rig.host.add_app(nested_sample_app());
    std::atomic<bool> release{false}, entered{false};
    Downloader inner = u.rig.host.downloader();
    Downloader slow = [&](const std::string& url, uint64_t max, long secs, const DownloadProgress& p) {
        if (url.find("/catalog.json") != std::string::npos && url.find(".sig") == std::string::npos) {
            entered = true;
            for (int i = 0; i < 100 && !release; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return inner(url, max, secs, p);
    };
    u.start(slow);
    PB_CHECK(u.open());
    UiClient c = u.client();
    HttpReply first;
    std::thread t([&] { first = u.plan("install", "testapp"); });
    for (int i = 0; i < 100 && !entered; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    PB_CHECK(entered);
    PB_CHECK(u.api->busy());
    PB_CHECK_EQ(c.post("api/list", "{}").status, 409);
    PB_CHECK_EQ(u.plan("update", "testapp").status, 409);
    J::Value state = c.get("api/state").json();
    PB_CHECK(state.get_bool("busy").value_or(false));
    HttpReply cancelled = c.post("api/cancel", "{}");
    PB_CHECK_EQ(cancelled.status, 200);
    release = true;
    t.join();
    PB_CHECK_EQ(first.status, 422);
    PB_CHECK(message_has_rule(first, "E-INS-33"));
    PB_CHECK(!c.get("api/state").json().get_bool("busy").value_or(true));
}

PB_TEST(error_answers_carry_the_three_parts_with_the_text_of_the_command_line) {
    Ui u;
    CliRig cli;
    if (!u.ready() || !cli.ready()) PB_SKIP("openssl CLI or the test program is not available");
    u.start();
    PB_CHECK(u.open());
    HttpReply r = u.plan("install", "no-such-app");
    PB_CHECK_EQ(r.status, 422);
    J::Value v = r.json();
    const J::Value* m = v.find("message");
    PB_CHECK(m != nullptr);
    for (const char* part : {"what", "reader_effect", "next"}) PB_CHECK(!m->get_string(part).value_or("").empty());
    PB_CHECK(has(m->get_string("what").value_or(""), "E-INS-28"));
    PB_CHECK(v.find("details") != nullptr);
    cli.host.add_app(nested_sample_app());
    cli.host.publish(100);
    ::mkdir((cli.dir.path() + "/home").c_str(), 0755);
    cli.write_reader();
    RunResult res = cli.run({"install", "no-such-app", "--reader", cli.root(), "--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"});
    PB_CHECK(has(res.err, m->get_string("what").value_or("?")));
    PB_CHECK(has(res.err, m->get_string("reader_effect").value_or("?")));
    PB_CHECK(has(res.err, m->get_string("next").value_or("?")));
}

PB_TEST(without_a_reader_the_operations_are_refused_and_bad_input_is_a_400) {
    Ui u;
    if (!u.ready()) PB_SKIP("openssl CLI not available");
    u.start();
    UiClient c = u.client();
    PB_CHECK_EQ(c.get("api/installed").status, 422);
    PB_CHECK_EQ(c.post("api/plan", "{\"action\":\"install\",\"app_id\":\"x\"}").status, 422);
    PB_CHECK(u.open());
    PB_CHECK_EQ(c.post("api/plan", "not json").status, 400);
    PB_CHECK_EQ(c.post("api/plan", "{\"action\":\"fly\",\"app_id\":\"x\"}").status, 400);
    PB_CHECK_EQ(c.post("api/execute", "{\"plan_id\":\"nope\",\"confirmations\":[]}").status, 422);
    PB_CHECK_EQ(c.get("api/nothing").status, 404);
    PB_CHECK_EQ(c.get("api/execute").status, 405);          // a state-changing path is POST only
}

PB_TEST_MAIN()
