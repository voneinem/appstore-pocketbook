// T134, T136, T151 (story 8, spec FR-037): Settings, About and the licenses. The controller exposes the facts of Settings read-only (no property or
// slot changes the catalog address, the keys or the trust anchors); the About notice is exactly the unofficial notice and the word PocketBook
// appears nowhere else in the pages except to name the device; the version is the compiled-in one; the license list is read from
// licenses/manifest.txt and lists every entry, and each entry opens its text.
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
#include "pbstore/store/about.h"
#include <QMetaObject>
#include <QMetaMethod>
#include <QMetaProperty>

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


struct Rig {
    StoreRig rig;
    std::unique_ptr<store::StoreService> service;
    std::unique_ptr<StoreController> controller;
    bool prepare() {
        app();
        if (!rig.ready()) return false;
        rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        store::StoreConfig config;
        config.trust_source = "the device's own certificate store";
        service = rig.make_service(config);
        controller = std::make_unique<StoreController>(*service, rig.screen, QStringLiteral("0.1.0"), QString::fromStdString(rig.root() + "/system/pbstore/app/pbstore.log"));
        controller->setLicensesRoot(QStringLiteral(PBSTORE_SOURCE_DIR));
        controller->startUp();
        return true;
    }
};

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

PB_TEST(settings_show_the_facts_read_only_and_nothing_can_change_the_address_the_keys_or_the_trust_anchors) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.controller->refresh();
    PB_CHECK(wait_for([&] { return !r.controller->busy() && r.controller->catalogLine().startsWith(QStringLiteral("Catalog last updated")); }));
    r.controller->loadSettings();
    const QVariantMap s = r.controller->settings();
    PB_CHECK_EQ(s.value(QStringLiteral("catalogUrl")).toString().toStdString(), r.rig.host.catalog_url());   // the compiled-in address
    PB_CHECK(s.value(QStringLiteral("refreshedAt")).toString() != QStringLiteral("never"));
    PB_CHECK(!s.value(QStringLiteral("generatedAt")).toString().isEmpty());
    PB_CHECK_EQ(s.value(QStringLiteral("trustSource")).toString().toStdString(), std::string("the device's own certificate store"));
    PB_CHECK_EQ(s.value(QStringLiteral("customCa")).toString().toStdString(), std::string("not supported"));
    PB_CHECK_EQ(s.value(QStringLiteral("logLocation")).toString().toStdString(), std::string("system/pbstore/app/pbstore.log"));
    PB_CHECK(s.contains(QStringLiteral("battery")) && s.contains(QStringLiteral("freeSpace")));
    // no writable property and no slot with such a name
    const QMetaObject* mo = r.controller->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {   // the controller's own properties
        const QMetaProperty p = mo->property(i);
        const QString name = QString::fromLatin1(p.name()).toLower();
        PB_CHECK(!p.isWritable());
        PB_CHECK(!(name.contains(QStringLiteral("catalogurl")) || name.contains(QStringLiteral("trustanchor")) || name.contains(QStringLiteral("cafile")) || name.contains(QStringLiteral("publickey"))));
    }
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {   // slots and invokables of the controller (signals only announce)
        if (mo->method(i).methodType() == QMetaMethod::Signal) continue;
        const QString name = QString::fromLatin1(mo->method(i).name()).toLower();
        if (name.startsWith(QStringLiteral("set")) && name != QStringLiteral("setlicensesroot")) PB_FAIL_AT("a setter is exposed: " + name.toStdString());
        PB_CHECK(!(name.contains(QStringLiteral("setcatalog")) || name.contains(QStringLiteral("setkey")) || name.contains(QStringLiteral("setca")) || name.contains(QStringLiteral("trustanchor"))));
    }
}

PB_TEST(the_about_notice_is_exact_the_version_is_the_compiled_in_one_and_pocketbook_is_named_nowhere_else) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    PB_CHECK_EQ(r.controller->aboutNotice().toStdString(), std::string("PB Store is unofficial and is not made, endorsed or supported by PocketBook."));
    PB_CHECK_EQ(r.controller->version().toStdString(), std::string("0.1.0"));
    // the pages: the word appears once in the About page (through the notice property) and only to name the device elsewhere
    int outside = 0;
    for (const char* page : {"Main", "pages/HomePage", "pages/DetailPage", "pages/SettingsPage", "pages/AboutPage", "pages/LicensesPage", "pages/LogPage", "pages/OperationPage", "pages/RepairPage"}) {
        const std::string text = slurp(std::string(PBSTORE_SOURCE_DIR) + "/qml/" + page + ".qml");
        for (size_t at = text.find("PocketBook"); at != std::string::npos; at = text.find("PocketBook", at + 1))
            if (text.compare(at, 18, "PocketBook InkPad ") != 0) ++outside;   // naming the device is allowed
    }
    PB_CHECK_EQ(outside, 0);   // the notice comes from the controller; the pages name PocketBook only as the maker of the device
    const std::string about = slurp(std::string(PBSTORE_SOURCE_DIR) + "/qml/pages/AboutPage.qml");
    PB_CHECK(about.find("aboutNotice") != std::string::npos);
}

PB_TEST(the_license_list_has_every_entry_of_the_manifest_and_each_entry_opens_its_text) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    const store::about::Manifest manifest = store::about::load_manifest(PBSTORE_SOURCE_DIR);
    PB_CHECK(manifest.readable && manifest.app && manifest.bad_blocks == 0);
    const QVariantList list = r.controller->licenses();
    PB_CHECK_EQ(static_cast<size_t>(list.size()), manifest.components.size());
    for (size_t i = 0; i < manifest.components.size(); ++i) {
        const QVariantMap row = list[static_cast<int>(i)].toMap();
        PB_CHECK_EQ(row.value(QStringLiteral("name")).toString().toStdString(), manifest.components[i].name);
        PB_CHECK_EQ(row.value(QStringLiteral("license")).toString().toStdString(), manifest.components[i].license);
        r.controller->loadLicense(row.value(QStringLiteral("id")).toString());
        PB_CHECK_EQ(r.controller->licenseTitle().toStdString(), manifest.components[i].name);
        if (manifest.components[i].text.empty()) {
            PB_CHECK(!r.controller->licenseAvailable());                                  // InkView: no text is published
            PB_CHECK(r.controller->licenseText().contains(QStringLiteral("no license text")));
        } else {
            PB_CHECK(r.controller->licenseAvailable());
            PB_CHECK(r.controller->licenseText().size() > 200);
        }
    }
    // the app's own license and an unknown id
    r.controller->loadLicense(QStringLiteral("pbstore"));
    PB_CHECK(r.controller->licenseAvailable());
    PB_CHECK(r.controller->licenseText().contains(QStringLiteral("Permission is hereby granted")));
    r.controller->loadLicense(QStringLiteral("no-such-entry"));
    PB_CHECK(!r.controller->licenseAvailable());
    PB_CHECK_EQ(r.controller->appLicense().value(QStringLiteral("copyright")).toString().toStdString(), manifest.app->copyright);
}

PB_TEST(without_a_manifest_the_license_list_is_empty_and_says_so) {
    Rig r;
    if (!r.prepare()) PB_SKIP("openssl CLI not available");
    r.controller->setLicensesRoot(QStringLiteral("/nonexistent-licenses-root"));
    PB_CHECK(r.controller->licenses().isEmpty());
    PB_CHECK(!r.controller->licensesAvailable());
}

PB_TEST_MAIN()
