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

    // Hands a web address to the reader's own browser (the detail page's source and changelog lines, after the user confirmed the address). Checked again
    // with is_followable_url (https only, one plain line, no credentials, at most 2048 bytes); false if it is refused or no browser could be started.
    // On the reader: OpenBook(BROWSER_FOR_AUTH, url, 0), the way PB Notes and InkFeed do it; the store stays open behind it. On the computer nothing is
    // opened (a test must never start a browser). The address is never written to the log.
    Q_INVOKABLE bool openUrl(const QString& url);

signals:
    void backRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};
