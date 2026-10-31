// PB Store on the reader: wires the engine of feature 002 to the store's service, the Qt layer and the pages. Everything with a decision
// is in core/store; this file only builds the objects and starts the window.
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
#include <thread>

#include "app/store_config.h"
#include "device_bridge.h"
#ifdef PBSTORE_TEST_BUILD
#include "ui_script.h"
#endif
#include "pbstore/install/clock.h"
#include "pbstore/install/engine.h"
#include "pbstore/install/log.h"
#include "pbstore/install/net.h"
#include "pbstore/install/posix_state_dirs.h"
#include "pbstore/store/store_redact.h"
#include "pbstore/store/catalog_cache.h"
#include "pbstore/store/last_fetch.h"
#include "pbstore/store/launch_notice.h"
#include "pbstore/store/pending_note.h"
#include "pbstore/store/proc_meminfo.h"
#include "pbstore/store/radio_session.h"
#include "pbstore/store/reader_storage.h"
#include "pbstore/store/run_state.h"
#include "pbstore/store/store_service.h"
#include "store_controller.h"
#include "table_translator.h"
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

// The launcher's notice mode (contracts/launcher.md S7): when the system refuses the GPU after a restart no Qt window can open, so the
// launcher asks this program to put a notice on the panel with InkView alone, before anything of Qt exists, and to end.
int show_launch_notice(const char* kind) {
    const auto notice = pbstore::store::launch_notice(kind);
    if (!notice) return 2;
#ifdef PBSTORE_ON_READER
    show_notice(notice->title, notice->text);
#else
    std::cout << notice->title << "\n" << notice->text << std::endl;
#endif
    return 0;
}

// The launcher's Wi-Fi mode (contracts/launcher.md): the reader ended the program at the home key while the store held Wi-Fi on, so the launcher
// asks the program to switch the radio off, with InkView alone and before anything of Qt exists. Status 0 only when the radio was seen off.
int switch_radio_off() {
#ifdef PBSTORE_ON_READER
    std::fprintf(stderr, "[pbstore] radio-off: opening the InkView session\n");
    open_inkview_session();
    InkViewRadio radio;
    const auto sleep = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    const auto now = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    const bool off = pbstore::store::RadioSession::switch_off(radio, sleep, now);
    std::fprintf(stderr, "[pbstore] radio-off: %s\n", off ? "the radio is off" : "the radio is still on or cannot be read");
    return off ? 0 : 1;
#else
    std::cout << "no radio on this computer" << std::endl;
    return 0;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--notice") return show_launch_notice(argv[2]);
    if (argc == 2 && std::string(argv[1]) == "--radio-off") return switch_radio_off();
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("pbstore"));
    // The texts of the interface: every qsTr()/tr() names a key of bridge/strings/strings.tsv (compiled in), looked up before anything is shown.
    TableTranslator strings;
    QFile strings_file(QStringLiteral(":/qml/strings.tsv"));
    const bool strings_ok = strings_file.open(QIODevice::ReadOnly) && strings.loadTable(strings_file.readAll()) && strings.entryCount() > 0;
    if (!strings_ok) std::cerr << "[pbstore] the table of interface texts is not available" << std::endl;
    app.installTranslator(&strings);
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
    pbstore::store::StoreRedactor redactor;
    if (const char* user = std::getenv("USER")) redactor.set_user_name(user);   // there is none on the reader; a host run has one
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
    pbstore::install::Downloader download = pbstore::install::make_https_downloader(net);
#ifdef PBSTORE_TEST_BUILD
    download = apply_test_host_map(download);
#endif
    deps.download = pbstore::store::observe_fetches(download, last_fetch, &redactor);
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
#ifdef PBSTORE_ON_READER
    config.reader_identity = [&identity] { return identity->describe(); };
    // what InkView gives for model and firmware, once per start, so that the real forms can be read from the mounted reader (device task DS-04;
    // printable ASCII and at most 40 characters of each, never the serial number)
    log.event("identity", "-", "-", "ok", {}, identity->describe());
#endif
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
        std::cerr << "[pbstore] about to quit" << std::endl;   // device task DS-13: what the reader sends at the home key
        controller.leave(0);
        screen.full_update();
    });
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &controller, [&controller](Qt::ApplicationState state) {
        std::cerr << "[pbstore] application state " << static_cast<int>(state) << std::endl;
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
        // every page is opened once on top of the list: a QML error in any of them fails the self-test
        static const char* const kPages[] = {"DetailPage", "SettingsPage", "AboutPage", "LicensesPage", "LogPage", "OperationPage", "RepairPage"};
        const int delay = screenshots.isEmpty() ? 60 : 500;
        QTimer::singleShot(screenshots.isEmpty() ? 300 : 900, &app, [&app, &controller, &qml, &qml_warnings, window, delay, screenshots, strings_ok] {   // by value: this block has ended when the timer fires
            const bool shown = window && window->property("pageLoaded").toBool();
            if (!shown) std::cerr << "[pbstore] no page is shown" << std::endl;
            QObject* root_object = qml.rootObjects().first();
            auto shot = [window, screenshots](const QString& name) {
                if (!screenshots.isEmpty() && window) window->grabWindow().save(screenshots + QLatin1Char('/') + name + QStringLiteral(".png"));
            };
            shot(QStringLiteral("01-home"));
            if (controller.model()->rowCount() > 0)
                controller.select(controller.model()->data(controller.model()->index(0, 0), AppListModel::AppIdRole).toString());
            controller.loadLicense(QStringLiteral("qt"));   // the page of one license has something to show
            const bool licenses = controller.licensesAvailable() && controller.licenseAvailable();   // the texts are compiled in
            if (!licenses) std::cerr << "[pbstore] the compiled-in license texts are not available" << std::endl;
            int step = 0;
            for (const char* page : kPages) {
                ++step;
                QTimer::singleShot(step * delay, &app, [=] {
                    QMetaObject::invokeMethod(root_object, "openPage", Q_ARG(QVariant, QString::fromLatin1(page)));
                    QTimer::singleShot(delay / 2, qApp, [=] {
                        shot(QStringLiteral("0%1-%2").arg(step + 1).arg(QString::fromLatin1(page)));
                        QMetaObject::invokeMethod(root_object, "closePage");
                    });
                });
            }
            QTimer::singleShot((step + 2) * delay, &app, [&app, shown, licenses, strings_ok, &qml_warnings] { app.exit((shown && qml_warnings == 0 && licenses && strings_ok) ? 0 : 2); });
        });
    }
#ifdef PBSTORE_TEST_BUILD
    // a scripted walk through the pages (tests/walks, scripts/ui-walk.sh): only the host test build has it
    if (const QByteArray script = qgetenv("PBSTORE_UI_SCRIPT"); !script.isEmpty())
        ui_script::start(qobject_cast<QQuickWindow*>(qml.rootObjects().first()), QString::fromLocal8Bit(script));
#endif
    return app.exec();
}
