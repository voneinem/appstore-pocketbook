// T100, T113, T119 (spec SC-002): the store adds nothing to what is written. The same fixture package on two equal fake readers, once
// through the engine directly and once through StoreService: for install, update, remove, the adoption of an app put there by hand, and the
// deletion of a data folder, the plan texts are equal and the resulting trees are equal (the records included: the rig's engine and the
// store's carry the same program name). Only the store's own state folder differs, and the engine's lock file.
#include <sys/stat.h>

#include <fstream>

#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

// The reader without the store's state folder (cache, log, notes) and the engine's lock; the records and the backups stay in.
Snapshot comparable(const Snapshot& s) {
    Snapshot out;
    for (const auto& i : s) {
        if (starts_with(i.path, "system/pbstore/app")) continue;
        if (i.path == "system/pbstore/lock") continue;
        out.push_back(i);
    }
    return out;
}

// Equal texts, or the first line in which they differ.
void same_text(const std::string& a, const std::string& b, const std::string& what) {
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    const size_t from = a.rfind('\n', i == 0 ? 0 : i - 1);
    const size_t line_a = from == std::string::npos ? 0 : from + 1;
    const size_t end_a = a.find('\n', i), end_b = b.find('\n', i);
    PB_FAIL_AT(what + ": the plan texts differ at\n  store: " + a.substr(line_a, (end_a == std::string::npos ? a.size() : end_a) - line_a) + "\n  engine: " +
               b.substr(line_a, (end_b == std::string::npos ? b.size() : end_b) - line_a));
}

struct Pair {
    StoreRig via_store, alone;
    std::unique_ptr<StoreService> service;
    CancelToken cancel;
    std::shared_ptr<ReaderHandle> handle;
    Engine* engine = nullptr;

    explicit Pair(const FakeReader& r) : via_store(r), alone(r) {}

    bool prepare(bool offer_v2) {
        if (!via_store.ready() || !alone.ready()) return false;
        for (StoreRig* rig : {&via_store, &alone}) {
            if (offer_v2) {
                CatalogEntry& e = rig->host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
                e.listed_versions = {"1.2.0", "1.3.0"};
            } else {
                rig->host.add_app(nested_sample_app("testapp", "1.2.0"));
            }
            rig->host.publish(100);
            rig->write_reader();
        }
        via_store.make_engine();
        service = via_store.make_service();
        service->start_check();
        CancelToken c;
        if (!service->refresh(c, nullptr).ok) throw std::runtime_error("fixture: refresh failed");
        engine = &alone.make_engine();
        auto opened = engine->open_reader(alone.root());
        handle = *opened.value;
        if (!engine->list(*handle, nullptr, cancel).ok()) throw std::runtime_error("fixture: the engine alone could not list");   // the same catalog in the cache
        return true;
    }

    // The engine alone: plan and execute with every confirmation the plan names, as the installer's --confirm options do.
    struct Alone {
        std::string text;
        bool ok = false;
        Plan plan;
    };
    Alone engine_run(Action::Type type) {
        Action act;
        act.type = type;
        act.app_id = "testapp";
        auto planned = engine->plan(*handle, act, nullptr, cancel);
        Alone a;
        if (!planned.ok()) return a;
        a.text = planned.value->text;
        a.plan = planned.value->plan;
        Confirmations given = planned.value->plan.confirmations;
        a.ok = engine->execute(*handle, *planned.value, given, nullptr, cancel).ok();
        return a;
    }
    PlanView store_plan(RowAction action) { return service->plan_action(action, "testapp", cancel, nullptr); }
    ExecView store_execute(const PlanView& p) {
        std::vector<std::string> names;
        for (const ConfirmationItem& i : p.confirmations) names.push_back(i.name);
        return service->execute_plan(names, false, cancel, nullptr);
    }
    void same_trees(const std::string& what) {
        Diff d = compare(comparable(snapshot(via_store.root())), comparable(snapshot(alone.root())));
        if (!d.identical) PB_FAIL_AT(what + ": the trees differ:\n" + d.describe());
    }
};

}  // namespace

PB_TEST(install_gives_the_same_plan_text_and_the_same_tree_as_the_engine_alone) {
    FakeReader r;
    r.add_data_dir("testapp", {{"notes.db", "precious"}});
    Pair p(r);
    if (!p.prepare(false)) PB_SKIP("openssl CLI not available");
    Pair::Alone a = p.engine_run(Action::Type::Install);
    PB_CHECK(a.ok);
    PlanView plan = p.store_plan(RowAction::Install);
    PB_CHECK(plan.ok);
    same_text(plan.text, a.text, "plan");
    PB_CHECK(p.store_execute(plan).ok);
    p.same_trees("install");
}

PB_TEST(update_gives_the_same_plan_text_and_the_same_tree_as_the_engine_alone) {
    FakeReader base;
    base.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
    Pair p(with_installed(base, nested_sample_app("testapp", "1.2.0")));
    if (!p.prepare(true)) PB_SKIP("openssl CLI not available");
    Pair::Alone a = p.engine_run(Action::Type::Update);
    PB_CHECK(a.ok);
    PlanView plan = p.store_plan(RowAction::Update);
    PB_CHECK(plan.ok);
    same_text(plan.text, a.text, "plan");
    PB_CHECK(p.store_execute(plan).ok);
    p.same_trees("update");
}

PB_TEST(remove_and_the_deletion_of_the_data_folder_give_the_same_plan_texts_and_trees_as_the_engine_alone) {
    FakeReader base;
    base.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
    Pair p(with_installed(base, nested_sample_app("testapp", "1.2.0")));
    if (!p.prepare(false)) PB_SKIP("openssl CLI not available");
    Pair::Alone a = p.engine_run(Action::Type::Remove);
    PB_CHECK(a.ok);
    PlanView plan = p.store_plan(RowAction::Remove);
    PB_CHECK(plan.ok);
    same_text(plan.text, a.text, "plan");
    PB_CHECK(p.store_execute(plan).ok);
    p.same_trees("remove");
    // the data folder stays with both; then the separate, confirmed deletion
    struct stat st;
    PB_CHECK_EQ(::stat((p.via_store.root() + "/system/testapp/notes.db").c_str(), &st), 0);
    PB_CHECK_EQ(::stat((p.alone.root() + "/system/testapp/notes.db").c_str(), &st), 0);
    auto planned = p.engine->plan_delete_data(*p.handle, a.plan);
    PB_CHECK(planned.ok());
    PlanView del = p.service->plan_delete_data();
    PB_CHECK(del.ok);
    PB_CHECK_EQ(del.data_folder, planned.value->plan.data->path);
    PB_CHECK_EQ(del.data_files, planned.value->plan.data->files);
    PB_CHECK(p.engine->execute_delete_data(*p.handle, *planned.value, delete_data_token_1(planned.value->plan), delete_data_token_2(planned.value->plan), nullptr, p.cancel).done);
    PB_CHECK(p.service->execute_delete_data(p.cancel, nullptr).ok);
    p.same_trees("delete data");
    PB_CHECK_EQ(::stat((p.via_store.root() + "/system/testapp").c_str(), &st), -1);
}

PB_TEST(the_adoption_of_an_app_put_there_by_hand_gives_the_same_plan_text_and_the_same_tree_as_the_engine_alone) {
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.2.0"));
    Pair p(hand);
    if (!p.prepare(false)) PB_SKIP("openssl CLI not available");
    Pair::Alone a = p.engine_run(Action::Type::Adopt);
    PB_CHECK(a.ok);
    PlanView plan = p.store_plan(RowAction::Check);
    PB_CHECK(plan.ok);
    same_text(plan.text, a.text, "plan");
    PB_CHECK(p.store_execute(plan).ok);
    p.same_trees("adopt");
}

PB_TEST_MAIN()
