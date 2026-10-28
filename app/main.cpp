// PB Store on the reader: wires the engine of feature 002 to the store's service, the Qt layer and the pages. Everything with a decision
// is in core/store; this file only builds the objects and starts the window.
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTimer>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include "app/store_config.h"
#include "device_bridge.h"
#include "pbstore/install/clock.h"
#include "pbstore/install/engine.h"
#include "pbstore/install/log.h"
#include "pbstore/install/net.h"
#include "pbstore/install/posix_state_dirs.h"
#include "pbstore/store/catalog_cache.h"
#include "pbstore/store/last_fetch.h"
#include "pbstore/store/pending_note.h"
#include "pbstore/store/proc_meminfo.h"
#include "pbstore/store/reader_storage.h"
#include "pbstore/store/run_state.h"
#include "pbstore/store/store_service.h"
#include "store_controller.h"
#include "pbstore_cacert.h"

#ifdef PBSTORE_ON_READER
#include "platform/pocketbook/inkview_devices.h"
#else
#include "host_devices.h"
#endif
#ifdef PBSTORE_TEST_BUILD
#include "pbstore/install/build_config.h"
#endif

namespace {

bool read_whole(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("pbstore"));
    const bool self_test = app.arguments().contains(QStringLiteral("--self-test"));
    QString screenshots;
    for (int i = 1; i + 1 < app.arguments().size(); ++i)
        if (app.arguments().at(i) == QLatin1String("--screenshots")) screenshots = app.arguments().at(i + 1);

    // The reader's own apps are set in Roboto: use it when the font database has it.
    if (QFontDatabase::families().contains(QStringLiteral("Roboto"))) {
        QFont font = app.font();
        font.setFamilies({QStringLiteral("Roboto")});
        app.setFont(font);
    }
    app.styleHints()->setCursorFlashTime(0);   // a blinking caret makes the e-ink panel refresh periodically

    // The root of everything the store writes is the reader's storage, fixed in the program. Only the host test build can name another.
    std::string root = "/mnt/ext1";
#ifdef PBSTORE_TEST_BUILD
    if (const QByteArray r = qgetenv("PBSTORE_TEST_READER_ROOT"); !r.isEmpty()) root = r.toStdString();
#endif
    pbstore::store::ReaderStorage storage = pbstore::store::make_reader_storage(root);
    pbstore::install::PosixStateDirs run_dirs(root + "/system/pbstore/app/run");
    QDir().mkpath(QString::fromStdString(root + "/system/pbstore/app/run"));   // the state folder and the launcher's folder

    // Trust anchors: the device's own certificate store when it is usable, else the bundled file. There is no other source (spec FR-004).
    std::string trust_source;
    pbstore::install::NetOptions net;
#ifdef PBSTORE_TEST_BUILD
    if (const std::string ca = pbstore::install::test_ca_file(); !ca.empty()) {
        net.ca_file = ca;
        trust_source = "a test file (test build)";
    } else
#endif
    {
        net = pbstore::install::choose_trust_anchors(pbstore::install::system_ca_bundle_candidates(), read_whole, pbstore_cacert_bundle());
        trust_source = net.ca_file.empty() ? "the bundled certificate file" : "the device's own certificate store";
    }
    net.user_agent.clear();   // no User-Agent: nothing but what an HTTPS request needs (spec FR-043)

    pbstore::install::SystemClock clock;
    pbstore::install::Redactor redactor;
    pbstore::install::Log log(*storage.state, clock, redactor, "pbstore.log");
    auto last_fetch = std::make_shared<pbstore::store::LastFetch>();
    pbstore::store::StoreCatalogCache cache(*storage.state, clock);
    pbstore::store::RunState run_state(*storage.state, run_dirs);
    pbstore::store::PendingNote pending_note(*storage.state);
    pbstore::store::ProcMeminfo memory;
#ifdef PBSTORE_ON_READER
    InkViewRadio radio;
    InkViewBattery battery;
    InkViewScreen screen;
    auto identity = make_inkview_identity();
#else
    HostRadio radio;
    HostBattery battery;
    HostScreen screen;
#endif

    pbstore::install::Deps deps;
    deps.open_fs = storage.open_fs;
    deps.mounts = storage.mounts.get();
    deps.state = storage.state.get();
    deps.download = pbstore::store::observe_fetches(pbstore::install::make_https_downloader(net), last_fetch);
    deps.clock = &clock;
    deps.log = &log;
    deps.redactor = &redactor;
#ifdef PBSTORE_ON_READER
    deps.identity_sources = {identity.get()};
#endif
    deps.catalog_cache = &cache;
    pbstore::install::Engine engine(pbstore::app::make_engine_config(), deps);

    pbstore::store::StoreConfig config = pbstore::app::make_store_config();
    config.trust_source = trust_source;
    pbstore::store::StoreParts parts{engine, storage, radio, battery, memory, screen, cache, run_state, pending_note, clock, last_fetch};
    pbstore::store::StoreService service(parts, config);
    StoreController controller(service, screen, QString::fromLatin1(PBSTORE_APP_VERSION), QString::fromStdString(root + "/system/pbstore/app/pbstore.log"));
    DeviceBridge device;
    app.installEventFilter(&device);

    QQmlApplicationEngine qml;
    int qml_warnings = 0;
    QObject::connect(&qml, &QQmlApplicationEngine::warnings, &app, [&](const QList<QQmlError>& list) {
        for (const QQmlError& e : list) std::cerr << "[pbstore] QML: " << e.toString().toStdString() << std::endl;
        qml_warnings += list.size();
    });
    qml.rootContext()->setContextProperty(QStringLiteral("store"), &controller);
    qml.rootContext()->setContextProperty(QStringLiteral("device"), &device);
    controller.startUp();   // the start check and the list from the cache: no network
    qml.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (qml.rootObjects().isEmpty()) {
        std::cerr << "[pbstore] failed to load QML" << std::endl;
        return 1;
    }

    // The user left: the home key, the reader going to sleep, the end of the app. All are handled the same way (spec FR-025): a download
    // stops and writes nothing, and Wi-Fi is restored by the session. Which events the reader sends is device task DS-13.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &controller, [&controller, &screen] {
        controller.leave(0);
        screen.full_update();
    });
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &controller, [&controller](Qt::ApplicationState state) {
        if (state == Qt::ApplicationSuspended) controller.leave(1);
        else if (state == Qt::ApplicationActive) controller.resumed();
    });

    // The first screen is up: the start counts as successful (the launcher's marker is removed, the version remembered).
    QTimer::singleShot(self_test ? 50 : 800, &app, [&controller] { controller.markStarted(); });

    if (self_test || !screenshots.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
        if (window && !screenshots.isEmpty()) {   // the size of the InkPad One's panel
            window->setVisibility(QWindow::Windowed);
            window->resize(1404, 1872);
        }
        QTimer::singleShot(screenshots.isEmpty() ? 300 : 900, &app, [&, window] {
            const bool shown = window && window->property("pageLoaded").toBool();
            if (!shown) std::cerr << "[pbstore] no page is shown" << std::endl;
            if (!screenshots.isEmpty() && window) {
                QObject* root_object = qml.rootObjects().first();
                auto shot = [window, screenshots](const QString& name) { window->grabWindow().save(screenshots + QLatin1Char('/') + name + QStringLiteral(".png")); };
                shot(QStringLiteral("01-home"));
                if (controller.model()->rowCount() > 0)
                    controller.select(controller.model()->data(controller.model()->index(0, 0), AppListModel::AppIdRole).toString());
                int step = 0;
                for (const char* page : {"DetailPage", "SettingsPage", "AboutPage", "LogPage"}) {
                    ++step;
                    QTimer::singleShot(step * 500, &app, [=] {
                        QMetaObject::invokeMethod(root_object, "openPage", Q_ARG(QVariant, QString::fromLatin1(page)));
                        QTimer::singleShot(250, qApp, [=] { shot(QStringLiteral("0%1-%2").arg(step + 1).arg(QString::fromLatin1(page))); QMetaObject::invokeMethod(root_object, "closePage"); });
                    });
                }
            }
            QTimer::singleShot(screenshots.isEmpty() ? 0 : 3500, &app, [&app, shown, &qml_warnings] { app.exit((shown && qml_warnings == 0) ? 0 : 2); });
        });
    }
    return app.exec();
}
