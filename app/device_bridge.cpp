#include "device_bridge.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>

#include "pbstore/store/link_url.h"

#ifdef PBSTORE_ON_READER
#include <inkview.h>
#endif

bool DeviceBridge::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Back || key == Qt::Key_Escape) {
            emit backRequested();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void DeviceBridge::quit() {
#ifdef PBSTORE_ON_READER
    // whiten the InkView canvas, then hand back through CloseApp() (adapted from the sibling apps)
    ClearScreen();
    FullUpdate();
    CloseApp();
#endif
    QCoreApplication::quit();
}

bool DeviceBridge::openUrl(const QString& url) {
    const QByteArray bytes = url.toUtf8();
    if (!pbstore::store::is_followable_url(std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())))) return false;
#ifdef PBSTORE_ON_READER
    OpenBook(BROWSER_FOR_AUTH, bytes.constData(), 0);   // the browser asks for the connection itself; this program holds no Wi-Fi for it
#endif
    return true;
}
