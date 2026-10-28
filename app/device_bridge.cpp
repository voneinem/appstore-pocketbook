#include "device_bridge.h"

#include <QEvent>
#include <QKeyEvent>

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
