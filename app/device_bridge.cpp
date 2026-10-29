#include "device_bridge.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>

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
