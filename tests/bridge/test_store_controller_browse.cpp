// T067, T068 (story 1): the list model and the browse controller. The model has the roles the pages use; every row has exactly one of the four state
// words, an app on the reader that the catalog does not list has its own label, and the icon is a separate role (never colour only); the note order
// is fixed. The controller refreshes on a worker thread, tells the catalog line, ignores a second action while one runs (FR-024), and applies a
// search only on confirm: one model update per call.
#include <QCoreApplication>
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



struct Rig {
    StoreRig rig;
    std::unique_ptr<store::StoreService> service;
    std::unique_ptr<StoreController> controller;
    explicit Rig(const FakeReader& r = FakeReader()) : rig(r) {}
    bool start(bool refresh = true) {
        app();
        if (!rig.ready()) return false;
        CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
        e.listed_versions = {"1.2.0", "1.3.0"};
        rig.host.add_app(nested_sample_app("secondapp", "1.0.0", "U_SecondApp"));
        rig.host.add_app(nested_sample_app("thirdapp", "1.0.0", "U_ThirdApp"));
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
        controller = std::make_unique<StoreController>(*service, rig.screen, QStringLiteral("0.1.0"), QString::fromStdString(rig.root() + "/system/pbstore/app/pbstore.log"));
        controller->startUp();
        if (refresh) {
            controller->refresh();
            return wait_for([&] { return !controller->busy() && controller->catalogLine().startsWith(QStringLiteral("Catalog last updated")); });
        }
        return true;
    }
};

}  // namespace

PB_TEST(without_a_catalog_the_list_shows_what_is_on_the_reader_and_the_line_says_to_refresh) {
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("handapp", "0.9.0", "U_HandApp"));
    Rig r(hand);
    if (!r.start(false)) PB_SKIP("openssl CLI not available");
    PB_CHECK_EQ(r.controller->catalogLine().toStdString(), std::string("No catalog yet. Tap Refresh."));
    const auto& rows = r.controller->model()->rows();
    PB_CHECK(!rows.empty());
    // without a catalog nothing is offered and an app on the reader is known only by its menu entry
    for (const auto& row : rows) PB_CHECK(row.state == store::RowState::NotInCatalog);
    bool found_hand = false;
    for (const auto& row : rows) found_hand = found_hand || row.id == "U_HandApp";
    PB_CHECK(found_hand);
}

PB_TEST(a_refresh_runs_on_a_worker_thread_and_a_second_action_while_it_runs_is_ignored) {
    Rig r;
    if (!r.start(false)) PB_SKIP("openssl CLI not available");
    r.controller->refresh();
    PB_CHECK(r.controller->busy());                                       // at once; the worker runs
    r.controller->refresh();                                              // ignored: one operation at a time
    r.controller->startAction(QStringLiteral("Install"), QStringLiteral("secondapp"));   // ignored too
    PB_CHECK(wait_for([&] { return !r.controller->busy(); }));
    PB_CHECK(r.controller->catalogLine().startsWith(QStringLiteral("Catalog last updated")));
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
    PB_CHECK_EQ(r.rig.connection_attempts(), 2);                          // the catalog and its signature, once
}

PB_TEST(the_model_has_the_roles_of_the_pages_and_every_offered_app_has_exactly_one_of_the_four_state_words) {
    Rig r(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));
    if (!r.start()) PB_SKIP("openssl CLI not available");
    const QHash<int, QByteArray> roles = r.controller->model()->roleNames();
    for (const char* name : {"appId", "name", "version", "shortDescription", "stateWords", "stateIcon", "notes", "offeredVersion", "installedVersion", "actions"})
        PB_CHECK(roles.values().contains(QByteArray(name)));
    const QStringList words = {QStringLiteral("Not installed"), QStringLiteral("Installed"), QStringLiteral("Update available"), QStringLiteral("Installed outside the store")};
    AppListModel* m = r.controller->model();
    PB_CHECK(m->rowCount() >= 3);
    for (int i = 0; i < m->rowCount(); ++i) {
        const QModelIndex ix = m->index(i, 0);
        if (m->rows()[static_cast<size_t>(i)].state == store::RowState::NotInCatalog) {   // the fifth label belongs to the apps the catalog does not list
            PB_CHECK_EQ(m->data(ix, AppListModel::StateWordsRole).toString().toStdString(), std::string("Not in the catalog"));
            continue;
        }
        const int hits = words.count(m->data(ix, AppListModel::StateWordsRole).toString());
        PB_CHECK_EQ(hits, 1);                                             // exactly one state word, never none or two
        PB_CHECK(!m->data(ix, AppListModel::StateIconRole).toString().isEmpty());   // the icon is a role of its own: never colour only
    }
    const auto* testapp = m->find(QStringLiteral("testapp"));
    PB_CHECK(testapp && testapp->state == store::RowState::UpdateAvailable);
    PB_CHECK_EQ(m->data(m->index(0, 0), AppListModel::StateWordsRole).toString().isEmpty(), false);
}

PB_TEST(a_search_applies_on_confirm_with_one_model_update_per_call_and_says_how_many_match) {
    Rig r;
    if (!r.start()) PB_SKIP("openssl CLI not available");
    int resets = 0;
    QObject::connect(r.controller->model(), &QAbstractItemModel::modelReset, [&resets] { ++resets; });
    const int all = r.controller->model()->rowCount();
    r.controller->search(QStringLiteral("second"));
    PB_CHECK_EQ(resets, 1);                                               // one update for the call
    PB_CHECK_EQ(r.controller->model()->rowCount(), 1);
    PB_CHECK_EQ(r.controller->searchSummary().toStdString(), QStringLiteral("1 of %1 apps").arg(all).toStdString());
    r.controller->search(QStringLiteral("second"));                      // the same text again changes nothing
    PB_CHECK_EQ(resets, 1);
    r.controller->search(QStringLiteral("no such thing"));
    PB_CHECK_EQ(r.controller->model()->rowCount(), 0);
    PB_CHECK_EQ(r.controller->searchSummary().toStdString(), std::string("No app matches"));
    r.controller->search(QString());
    PB_CHECK_EQ(r.controller->model()->rowCount(), all);
    PB_CHECK_EQ(resets, 3 + 0);
}

PB_TEST_MAIN()
