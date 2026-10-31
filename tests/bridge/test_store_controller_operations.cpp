// The Qt controller over the real service and a fake reader: the operation from the tap on a button to the result, on a worker thread,
// with the phases the pages follow (planning, plan, executing, result). One operation at a time (spec FR-024); nothing is written before
// Confirm; a failure ends with the three parts; the data folder is deleted only after two confirmations; update all stops at the first failure.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <fstream>
#include <functional>
#include <sys/stat.h>

#include "store_controller.h"
#undef emit   // a Qt macro; the test support has a method of that name
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbtest;

namespace {

QCoreApplication& app() {
    static int argc = 1;
    static char name[] = "test";
    static char* argv[] = {name, nullptr};
    static QCoreApplication a(argc, argv);
    return a;
}

bool wait_for(const std::function<bool()>& done, int ms = 20000) {
    app();
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// The nested sample app with `extra` more files, so that its plan lists more files than the plan page shows unfolded.
SampleApp app_with_files(size_t extra, const std::string& version = "1.2.0") {
    SampleApp app = nested_sample_app("testapp", version);
    for (size_t i = 0; i < extra; ++i)
        app.files.push_back(file("applications/testapp/lib/part" + std::to_string(i) + ".bin", pseudo_bytes(300, static_cast<unsigned>(i) + 100)));
    app.manifest.set("files", files_value(app.files));
    return app;
}

const QStringList kConsents = {QStringLiteral("unknown-model"), QStringLiteral("unknown-firmware")};

struct Rig {
    StoreRig rig;
    std::unique_ptr<store::StoreService> service;
    std::unique_ptr<StoreController> controller;
    explicit Rig(const FakeReader& r = FakeReader()) : rig(r) {}
    bool prepare(bool with_v2 = false, const SampleApp* catalog_app = nullptr) {
        app();
        if (!rig.ready()) return false;
        if (with_v2) {
            install::CancelToken unused;
            CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
            e.listed_versions = {"1.2.0", "1.3.0"};
        } else if (catalog_app) {
            rig.host.add_app(*catalog_app);
        } else {
            rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        }
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
        controller = std::make_unique<StoreController>(*service, rig.screen, QStringLiteral("0.1.0"), QString::fromStdString(rig.root() + "/system/pbstore/app/pbstore.log"));
        controller->startUp();
        controller->refresh();
        if (!wait_for([&] { return !controller->busy() && controller->model()->rowCount() > 0 && controller->catalogLine().startsWith(QStringLiteral("Catalog last updated")); })) return false;
        return true;
    }
    bool phase(const char* p) { return wait_for([&] { return controller->phase() == QLatin1String(p); }); }
};

}  // namespace

PB_TEST(an_install_goes_from_planning_to_the_plan_to_the_result_and_back_to_the_list) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));
    PB_CHECK(r.controller->phase() == QLatin1String("planning"));       // at once; the worker runs
    PB_CHECK(r.phase("plan"));
    PB_CHECK(r.controller->planText().contains(QStringLiteral("Plan: Install")));
    PB_CHECK(r.controller->confirmations().size() >= 1);
    PB_CHECK(!r.controller->busy());
    struct stat st;
    PB_CHECK_EQ(::stat((r.rig.root() + "/applications/testapp.app").c_str(), &st), -1);   // nothing is written before Confirm
    r.controller->confirm(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultOk());
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("is installed")));
    PB_CHECK_EQ(::stat((r.rig.root() + "/applications/testapp.app").c_str(), &st), 0);
    r.controller->finishResult();
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
    const store::AppRow* row = r.controller->model()->find(QStringLiteral("testapp"));
    PB_CHECK(row && row->state == store::RowState::Installed);
}

PB_TEST(declining_the_plan_writes_nothing_and_returns_to_the_list) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("plan"));
    r.controller->decline();
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
    struct stat st;
    PB_CHECK_EQ(::stat((r.rig.root() + "/applications/testapp.app").c_str(), &st), -1);
}

PB_TEST(a_second_action_while_one_runs_is_ignored) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));   // ignored
    r.controller->refresh();                                                          // ignored too
    PB_CHECK(r.phase("plan"));
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));   // a plan is waiting: ignored
    PB_CHECK(r.controller->phase() == QLatin1String("plan"));
    r.controller->decline();
}

PB_TEST(a_refusal_ends_in_the_result_with_the_three_parts_and_nothing_was_changed) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.rig.battery.set_percent(20);
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("result"));
    PB_CHECK(!r.controller->resultOk());
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("E-STO-01")));
    PB_CHECK(r.controller->resultReader().contains(QStringLiteral("not changed")));
    PB_CHECK(r.controller->resultTodo().contains(QStringLiteral("Charge the reader")));
    r.controller->finishResult();
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
}

PB_TEST(removing_keeps_the_data_and_deleting_it_takes_two_confirmations) {
    Rig r(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    ::mkdir((r.rig.root() + "/system/testapp").c_str(), 0755);
    { std::ofstream f(r.rig.root() + "/system/testapp/user.txt"); f << "data"; }
    r.controller->startAction(QStringLiteral("Remove"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("plan"));
    r.controller->confirm(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultOk());
    PB_CHECK(r.controller->canDeleteData());
    r.controller->askDeleteData();
    PB_CHECK_EQ(r.controller->deleteStep(), 1);
    PB_CHECK(r.controller->dataFolderText().contains(QStringLiteral("system/testapp")));
    r.controller->confirmDeleteData();                                   // the first confirmation only moves to the second
    PB_CHECK_EQ(r.controller->deleteStep(), 2);
    struct stat st;
    PB_CHECK_EQ(::stat((r.rig.root() + "/system/testapp/user.txt").c_str(), &st), 0);   // still there
    r.controller->confirmDeleteData();
    PB_CHECK(r.phase("result"));
    PB_CHECK(wait_for([&] { return !r.controller->busy(); }));
    PB_CHECK_EQ(::stat((r.rig.root() + "/system/testapp").c_str(), &st), -1);            // now it is gone
    r.controller->finishResult();
}

PB_TEST(update_all_updates_the_apps_one_after_the_other_with_a_plan_and_a_summary) {
    Rig r(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));
    if (!r.prepare(true)) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.controller->canUpdateAll());
    r.controller->startUpdateAll();
    PB_CHECK(r.phase("plan"));
    PB_CHECK(r.controller->operationTitle().contains(QStringLiteral("Updating 1 of 1")));
    r.controller->confirm(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("Updated:")));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("App testapp")));
    r.controller->finishResult();
    PB_CHECK(!r.controller->canUpdateAll());
}

PB_TEST(cancel_is_offered_until_the_engine_reports_the_commit_point_and_not_after) {
    // the page then shows "Finishing; this cannot be cancelled" (OperationPage.qml, visible: !cancellable); the figures are tested in test_progress_text
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("plan"));
    PB_CHECK(r.controller->cancellable());                               // an operation that can still be undone
    r.controller->confirm(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(wait_for([&] { return !r.controller->busy(); }));
    PB_CHECK(!r.controller->cancellable());                              // the engine passed "finishing" on the way
    r.controller->finishResult();
}

PB_TEST(declining_an_app_in_update_all_skips_it) {
    Rig r(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));
    if (!r.prepare(true)) PB_SKIP("openssl CLI not available");
    r.controller->startUpdateAll();
    PB_CHECK(r.phase("plan"));
    r.controller->decline();
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("Nothing was updated")));
    r.controller->finishResult();
    PB_CHECK(r.controller->canUpdateAll());                              // still outdated: nothing was changed
}

PB_TEST(the_plan_page_shows_plain_sentences_first_and_the_installers_text_with_every_file_only_behind_show_details) {
    Rig r;
    const SampleApp big = app_with_files(30);
    if (!r.prepare(false, &big)) PB_SKIP("openssl CLI not available");
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("plan"));
    const QString summary = r.controller->planSummary();
    PB_CHECK(summary.contains(QStringLiteral("Install App testapp 1.2.0")));
    PB_CHECK(summary.contains(QStringLiteral("From the PB Store catalog")));
    for (const char* technical : {"applications/", "sha256", "part0.bin", "E-INS", "Folders to create"})
        PB_CHECK(!summary.contains(QLatin1String(technical)));
    PB_CHECK(!r.controller->planDetailsShown());              // the page shows planText only when this is true
    PB_CHECK(r.controller->planText().contains(QStringLiteral("part0.bin")));   // the engine's full text, unchanged
    PB_CHECK(r.controller->planText().contains(QStringLiteral("Plan: Install")));
    r.controller->toggleDetails();
    PB_CHECK(r.controller->planDetailsShown());
    r.controller->toggleDetails();
    PB_CHECK(!r.controller->planDetailsShown());
    r.controller->decline();
}

PB_TEST(a_new_plan_starts_with_the_details_hidden) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("plan"));
    r.controller->toggleDetails();
    r.controller->decline();
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("plan"));
    PB_CHECK(!r.controller->planDetailsShown());
    r.controller->decline();
}

PB_TEST(the_word_Replace_starts_a_replace_plan_for_an_app_put_there_by_hand) {
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.1.0"));      // other files than the catalog's package
    Rig r(hand);
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    const store::AppRow* row = r.controller->model()->find(QStringLiteral("testapp"));
    PB_CHECK(row && row->state == store::RowState::InstalledOutside);
    r.controller->startAction(QStringLiteral("Replace"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("plan"));
    PB_CHECK(r.controller->planText().contains(QStringLiteral("Plan: Replace")));
    PB_CHECK(r.controller->planSummary().contains(QStringLiteral("Replace App testapp")));
    bool asksToReplace = false;
    for (const QVariant& c : r.controller->confirmations())
        if (c.toMap().value(QStringLiteral("name")).toString() == QLatin1String("replace")) asksToReplace = true;
    PB_CHECK(asksToReplace);
    r.controller->decline();
}

PB_TEST_MAIN()
