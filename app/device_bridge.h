#pragma once
// Device glue that Qt does not give: the hardware or Escape Back key as a signal for the pages, and leaving the app (the Exit control; the
// reader's own home key too, where it has one, spec FR-025).

#include <QObject>

class DeviceBridge : public QObject {
    Q_OBJECT
public:
    explicit DeviceBridge(QObject* parent = nullptr) : QObject(parent) {}

    // The Exit control: leaves the store. On the reader the panel is whitened and the app handed back to the launcher
    // (an EGL app otherwise leaves a black framebuffer, as in the sibling apps); on the computer the application quits.
    Q_INVOKABLE void quit();

signals:
    void backRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};
