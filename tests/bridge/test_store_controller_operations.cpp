// The Qt controller over the real service and a fake reader: the operation from the tap on a button to the result, on a worker thread,
// with the phases the pages follow (planning, plan, executing, result). One operation at a time (spec FR-024); nothing is written before
// Confirm; a failure ends with the three parts; the data folder is deleted only after two confirmations; update all stops at the first failure.
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include <fstream>
#include <functional>
#include <sys/stat.h>

#include "store_controller.h"
#include "qt_strings.h"
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
    install_strings();
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
    // Feature 004: several installed apps (1.2.0), each with 1.3.0 offered; the package of `broken` is garbage, so its plan fails the hash check.
    bool prepare_apps(const std::vector<std::string>& outdated, const std::string& broken = std::string()) {
        app();
        if (!rig.ready()) return false;
        for (const std::string& id : outdated) {
            CatalogEntry& e = rig.host.add_app(nested_sample_app_v2(id, "U_" + id, "1.3.0"));
            e.listed_versions = {"1.2.0", "1.3.0"};
        }
        rig.host.publish(100);
        if (!broken.empty())
            for (const CatalogEntry& e : rig.host.catalog.apps)
                if (e.id == broken) rig.host.set_raw(package_path_of(e), "this is not the package");
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
        controller = std::make_unique<StoreController>(*service, rig.screen, QStringLiteral("0.1.0"), QString::fromStdString(rig.root() + "/system/pbstore/app/pbstore.log"));
        controller->startUp();
        controller->refresh();
        return wait_for([&] { return !controller->busy() && controller->model()->rowCount() > 0 && controller->catalogLine().startsWith(QStringLiteral("Catalog last updated")); });
    }
};

// The reader with each of `ids` installed at 1.2.0.
FakeReader installed_apps(const std::vector<std::string>& ids) {
    FakeReader r;
    for (const std::string& id : ids) r = with_installed(r, nested_sample_app(id, "1.2.0", "U_" + id));
    return r;
}

// The reader's files without the store's own state, the journal's folder, the lock and the backups (as in test_store_service_actions.cpp).
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

// Every phase the controller passes through, in order, without repeats.
struct PhaseLog {
    QStringList phases;
    PhaseLog(StoreController& c) {
        QObject::connect(&c, &StoreController::operationChanged, [this, &c] {
            if (phases.isEmpty() || phases.last() != c.phase()) phases << c.phase();
        });
    }
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

PB_TEST(update_all_asks_once_and_then_updates_the_apps_one_after_the_other_with_no_plan_page_between) {
    Rig r(installed_apps({"testapp", "secondapp"}));
    if (!r.prepare_apps({"testapp", "secondapp"})) PB_SKIP("openssl CLI not available");
    PB_CHECK(r.controller->canUpdateAll());
    const size_t requests = r.rig.requests().size();
    const Snapshot before = snapshot(r.rig.root());
    r.controller->startUpdateAll();
    PB_CHECK(r.controller->phase() == QLatin1String("updateAllPlan"));      // at once: the combined confirmation, nothing runs
    PB_CHECK(!r.controller->busy());
    const QVariantList entries = r.controller->updateAllEntries();
    PB_CHECK_EQ(entries.size(), 2);
    PB_CHECK_EQ(entries[0].toMap().value(QStringLiteral("name")).toString().toStdString(), std::string("App secondapp"));   // by name; no PB Store here
    PB_CHECK_EQ(entries[0].toMap().value(QStringLiteral("from")).toString().toStdString(), std::string("1.2.0"));
    PB_CHECK_EQ(entries[0].toMap().value(QStringLiteral("to")).toString().toStdString(), std::string("1.3.0"));
    PB_CHECK(!entries[0].toMap().value(QStringLiteral("size")).toString().isEmpty());
    PB_CHECK(r.controller->updateAllLines().contains(QStringLiteral("The data of your apps is kept.")));
    PB_CHECK(r.controller->updateAllLines().contains(QStringLiteral("Wi-Fi is switched on only while packages are downloaded.")));
    PB_CHECK_EQ(r.rig.requests().size(), requests);                          // nothing was downloaded before the confirmation
    PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);   // and nothing was written
    PhaseLog log(*r.controller);
    r.controller->confirmUpdateAll(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(!log.phases.contains(QStringLiteral("plan")));                  // no plan page between the apps
    PB_CHECK(r.controller->resultOk());
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("Updated:")));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("App testapp")));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("App secondapp")));
    r.controller->finishResult();
    PB_CHECK(!r.controller->canUpdateAll());
    PB_CHECK_EQ(r.controller->updateCount(), 0);
}

PB_TEST(the_progress_title_of_an_update_all_run_says_which_app_of_how_many) {
    Rig r(installed_apps({"testapp", "secondapp"}));
    if (!r.prepare_apps({"testapp", "secondapp"})) PB_SKIP("openssl CLI not available");
    r.controller->startUpdateAll();
    QStringList titles;
    QObject::connect(r.controller.get(), &StoreController::operationChanged, [&] { if (titles.isEmpty() || titles.last() != r.controller->operationTitle()) titles << r.controller->operationTitle(); });
    r.controller->confirmUpdateAll(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(titles.contains(QStringLiteral("Updating 1 of 2: App secondapp")));
    PB_CHECK(titles.contains(QStringLiteral("Updating 2 of 2: App testapp")));
}

PB_TEST(cancelling_the_combined_confirmation_changes_nothing_and_uses_no_network) {
    Rig r(installed_apps({"testapp"}));
    if (!r.prepare_apps({"testapp"})) PB_SKIP("openssl CLI not available");
    const size_t requests = r.rig.requests().size();
    const Snapshot before = snapshot(r.rig.root());
    r.controller->startUpdateAll();
    PB_CHECK(r.controller->phase() == QLatin1String("updateAllPlan"));
    r.controller->cancelUpdateAll();
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
    PB_CHECK_EQ(r.rig.requests().size(), requests);
    PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);
    PB_CHECK(r.controller->canUpdateAll());                                  // still outdated
}

PB_TEST(update_all_does_not_start_without_a_consent_that_the_confirmation_offered) {
    Rig r(installed_apps({"testapp"}));
    if (!r.prepare_apps({"testapp"})) PB_SKIP("openssl CLI not available");
    r.controller->startUpdateAll();
    PB_CHECK_EQ(r.controller->updateAllConsents().size(), 2);                // the fake reader has no identity: both are offered
    r.controller->confirmUpdateAll(QStringList{QStringLiteral("unknown-model")}, false);   // unknown-firmware is missing
    PB_CHECK(r.controller->phase() == QLatin1String("updateAllPlan"));      // refused: the page's Confirm is disabled then as well
    r.controller->cancelUpdateAll();
}

PB_TEST(a_single_update_still_shows_its_plan_and_needs_its_own_confirmation) {
    // regression pin: only Update all lost its per-app plan page
    Rig r(installed_apps({"testapp"}));
    if (!r.prepare_apps({"testapp"})) PB_SKIP("openssl CLI not available");
    r.controller->startAction(QStringLiteral("Update"), QStringLiteral("testapp"));
    PB_CHECK(r.phase("plan"));
    PB_CHECK(r.controller->planSummary().contains(QStringLiteral("Update App testapp")));
    r.controller->confirm(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultOk());
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

PB_TEST(an_operation_cut_short_by_a_leave_does_not_leave_the_store_busy_after_it_is_resumed) {
    // the reader's sleep or the Home button: the worker ends without reporting (nothing is shown while leaving); on wake the buttons must work again
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.controller->leave(1);
    r.controller->refresh();
    PB_CHECK(r.controller->busy());
    r.controller->resumed();
    PB_CHECK(!r.controller->busy());
    PB_CHECK_EQ(r.controller->phase().toStdString(), std::string("idle"));
    r.controller->refresh();                                             // and the next operation runs through
    PB_CHECK(wait_for([&] { return !r.controller->busy(); }));
    PB_CHECK(r.controller->catalogLine().contains(QStringLiteral("Catalog last updated")));
}

PB_TEST(leaving_through_the_house_icon_ends_the_wifi_session_before_the_program_is_closed) {
    // 2026-10-31, reader: the house icon calls InkView's CloseApp(), which ends the process at once; nothing of the session ran, Wi-Fi stayed on
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    store::StoreConfig real_time;                                        // this test waits for real: the refresh is held in the wait for the connection
    real_time.sleep_ms = [](int ms) { QThread::msleep(static_cast<unsigned long>(ms)); };
    real_time.now_ms = [] { return QDateTime::currentMSecsSinceEpoch(); };
    auto service = r.rig.make_service(real_time);
    StoreController controller(*service, r.rig.screen, QStringLiteral("0.1.0"), QString::fromStdString(r.rig.root() + "/system/pbstore/app/pbstore.log"));
    controller.startUp();
    const int earlier = r.rig.radio.count("connect");                    // the refresh of prepare() has used the radio once
    r.rig.radio.set_connect(FakeRadio::Connect::NeverConnects);
    controller.refresh();
    PB_CHECK(wait_for([&] { return r.rig.radio.count("connect") == earlier + 1; }));
    PB_CHECK(r.rig.radio.powered());
    PB_CHECK(r.rig.run_state->radio_marker());
    controller.leaveAndWait();                                           // what the icon does before it closes the program
    PB_CHECK(!r.rig.radio.powered());                                    // the session ended: Wi-Fi is back as it was
    PB_CHECK(!r.rig.run_state->radio_marker());
}

PB_TEST(an_app_whose_update_needs_a_consent_of_its_own_pauses_the_run_for_that_app_only_and_declining_skips_it) {
    // aaedited sorts first and has a recorded file the user changed (E-INS-32); testapp is clean
    FakeReader reader = installed_apps({"aaedited", "testapp"});
    reader.files["applications/aaedited.bin"] = "edited by hand";
    Rig r(reader);
    if (!r.prepare_apps({"aaedited", "testapp"})) PB_SKIP("openssl CLI not available");
    r.controller->startUpdateAll();
    r.controller->confirmUpdateAll(kConsents, false);
    PB_CHECK(r.phase("plan"));                                              // the run pauses at the app with the changed file
    PB_CHECK(r.controller->operationTitle().contains(QStringLiteral("App aaedited")));
    QStringList names;
    for (const QVariant& c : r.controller->confirmations()) names << c.toMap().value(QStringLiteral("name")).toString();
    PB_CHECK(names.contains(QStringLiteral("edited-files")));
    PB_CHECK(!names.contains(QStringLiteral("unknown-model")));              // given once for the run: not asked again for this app
    PB_CHECK(!names.contains(QStringLiteral("unknown-firmware")));
    r.controller->decline();                                                // skip it; the run goes on
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("Updated: App testapp")));
    PB_CHECK(!r.controller->resultText().contains(QStringLiteral("Failed")));
    r.controller->finishResult();
    const store::AppRow* skipped = r.controller->model()->find(QStringLiteral("aaedited"));
    PB_CHECK(skipped && skipped->state == store::RowState::UpdateAvailable);   // still outdated: nothing was changed
    PB_CHECK_EQ(r.controller->updateCount(), 1);
}

PB_TEST(confirming_the_paused_app_adds_the_consents_given_for_the_run_and_updates_it) {
    FakeReader reader = installed_apps({"aaedited", "testapp"});
    reader.files["applications/aaedited.bin"] = "edited by hand";
    Rig r(reader);
    if (!r.prepare_apps({"aaedited", "testapp"})) PB_SKIP("openssl CLI not available");
    r.controller->startUpdateAll();
    r.controller->confirmUpdateAll(kConsents, false);
    PB_CHECK(r.phase("plan"));
    // the page passes only the consent that it showed; the reader consents given for the run are added by the controller
    r.controller->confirm(QStringList{QStringLiteral("edited-files")}, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultOk());
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("App aaedited")));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("App testapp")));
    r.controller->finishResult();
    PB_CHECK_EQ(r.controller->updateCount(), 0);
}

PB_TEST(a_failing_app_stops_the_run_and_the_summary_says_updated_failed_and_not_tried) {
    Rig r(installed_apps({"aapp", "bapp", "capp"}));
    if (!r.prepare_apps({"aapp", "bapp", "capp"}, "bapp")) PB_SKIP("openssl CLI not available");
    r.controller->startUpdateAll();
    r.controller->confirmUpdateAll(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(!r.controller->resultOk());
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("Updated: App aapp")));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("Failed: App bapp")));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("Not tried: App capp")));
    PB_CHECK(r.controller->resultReader().contains(QStringLiteral("not changed")));
    r.controller->finishResult();
    PB_CHECK_EQ(r.controller->updateCount(), 2);                             // bapp and capp still have an update
}

PB_TEST(a_wifi_that_could_not_be_switched_off_is_still_shown_on_the_result_page_of_a_run) {
    // constitution Principle V: the notice of the plan must not be lost on the path that skips the plan page
    Rig r(installed_apps({"testapp", "secondapp"}));
    if (!r.prepare_apps({"testapp", "secondapp"})) PB_SKIP("openssl CLI not available");
    r.rig.radio.set_disconnect_works(false);
    r.controller->startUpdateAll();
    r.controller->confirmUpdateAll(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultOk());
    PB_CHECK(r.controller->notice().contains(QStringLiteral("could not be switched off")));
    r.controller->finishResult();
}

PB_TEST(with_one_app_a_wifi_that_could_not_be_switched_off_leaves_the_marker_for_the_launcher) {
    // (with two apps the second plan finds the radio already on and, as RadioSession::begin does, no longer acts on the marker)
    Rig r(installed_apps({"testapp"}));
    if (!r.prepare_apps({"testapp"})) PB_SKIP("openssl CLI not available");
    r.rig.radio.set_disconnect_works(false);
    r.controller->startUpdateAll();
    r.controller->confirmUpdateAll(kConsents, false);
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->notice().contains(QStringLiteral("could not be switched off")));
    PB_CHECK(r.rig.run_state->radio_marker());                               // the launcher tries once more
    r.controller->finishResult();
}

PB_TEST(starting_update_all_while_an_operation_runs_is_ignored) {
    Rig r(installed_apps({"testapp"}));
    if (!r.prepare_apps({"testapp"})) PB_SKIP("openssl CLI not available");
    r.controller->refresh();
    PB_CHECK(r.controller->busy());
    r.controller->startUpdateAll();                                          // ignored: one operation at a time
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
    PB_CHECK(wait_for([&] { return !r.controller->busy(); }));
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
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
