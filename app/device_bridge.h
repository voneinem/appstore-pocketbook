#pragma once
// Device glue that Qt does not give: the hardware or Escape Back key as a signal for the pages. The reader has no hardware Back button, so
// this matters on the computer; there is no exit control in the store, the home key of the reader leaves it (spec FR-025).

#include <QObject>

class DeviceBridge : public QObject {
    Q_OBJECT
public:
    explicit DeviceBridge(QObject* parent = nullptr) : QObject(parent) {}

signals:
    void backRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};
