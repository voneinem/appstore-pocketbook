#include "store_controller.h"

#include <QDateTime>
#include <QFile>
#include <QMetaObject>
#include <QStringList>

#include "pbstore/store/app_filter.h"
#include "pbstore/store/store_rules.h"

using pbstore::store::AppRow;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

QString sizeText(int64_t bytes) {
    if (bytes <= 0) return QString();
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

// "2026-10-25T12:00:00Z" -> "2026-10-25" (the date is what the list says)
QString dateOf(const std::string& iso) { return q(iso.substr(0, 10)); }
}  // namespace

StoreController::StoreController(pbstore::store::StoreService& service, pbstore::store::Screen& screen, const QString& version, const QString& logPath, QObject* parent)
    : QObject(parent), service_(service), screen_(screen), version_(version), logPath_(logPath) {}

StoreController::~StoreController() {
    leaving_ = true;
    cancel_->cancel();
    if (worker_.joinable()) worker_.join();
}

QString StoreController::catalogLine() const { return catalogLine_; }
QString StoreController::searchSummary() const { return summary_; }

void StoreController::setBusy(bool b) {
    if (busy_ == b) return;
    busy_ = b;
    emit busyChanged();
}

void StoreController::fullRefresh(bool wanted) {
    if (wanted) screen_.full_update();
}

void StoreController::pageKind(const QString& kind) {
    using pbstore::store::PageKind;
    PageKind k = PageKind::Other;
    if (kind == QLatin1String("list")) k = PageKind::List;
    else if (kind == QLatin1String("plan")) k = PageKind::Plan;
    else if (kind == QLatin1String("progress")) k = PageKind::Progress;
    else if (kind == QLatin1String("result")) k = PageKind::Result;
    fullRefresh(policy_.show(k));
}

void StoreController::pageTurned() { fullRefresh(policy_.page_turn()); }

void StoreController::markStarted() {
    // the start check and the first screen are done (spec FR-034)
    fullRefresh(policy_.start());
    if (!version_.isEmpty()) {
        service_.mark_started(version_.toStdString());
    }
}

void StoreController::showMessage(const pbstore::install::Message& m) {
    messageTitle_ = q(m.what);
    messageReader_ = q(m.reader_effect);
    messageTodo_ = q(m.next);
    emit messageChanged();
}

void StoreController::clearMessage() {
    messageTitle_.clear();
    messageReader_.clear();
    messageTodo_.clear();
    emit messageChanged();
}

void StoreController::addNotices(const std::vector<pbstore::Finding>& notices) {
    QStringList lines;
    for (const pbstore::Finding& n : notices) {
        if (n.rule_id == "W-INS-03") continue;   // the first-run notice is for the log, not for the screen
        lines << q(pbstore::store::store_message(n).what);
    }
    notice_ = lines.join(QLatin1Char('\n'));
    emit noticeChanged();
}

void StoreController::applyView(const pbstore::store::ListView& view) {
    lastView_ = view;
    catalogLoaded_ = view.catalog.loaded;
    if (view.catalog.loaded) catalogLine_ = tr("Catalog last updated %1").arg(view.catalog.refreshed_at.empty() ? tr("(date unknown)") : dateOf(view.catalog.refreshed_at));
    else if (view.catalog.discarded) catalogLine_ = tr("The saved catalog was discarded. Tap Refresh.");
    else catalogLine_ = tr("No catalog yet. Tap Refresh.");
    const std::vector<AppRow> shown = service_.rows(search_.toStdString());
    model_.setRows(shown);
    if (search_.isEmpty()) summary_ = view.rows.empty() ? tr("The catalog has no apps") : QString();
    else if (shown.empty()) summary_ = tr("No app matches");
    else summary_ = tr("%1 of %2 apps").arg(shown.size()).arg(view.rows.size());
    emit viewChanged();
}

void StoreController::startUp() {
    startResult_ = service_.start_check();
    repairPending_ = startResult_.repair_pending;
    applyView(service_.list_cached());
    std::vector<pbstore::Finding> notices = startResult_.notices;
    for (const pbstore::Finding& n : lastView_.notices) notices.push_back(n);
    if (repairPending_)
        notices.push_back(pbstore::store::make_store_finding("E-INS-23", "reader", "An earlier operation on this reader was interrupted and has to be repaired first."));
    addNotices(notices);
}

void StoreController::search(const QString& text) {
    if (search_ == text) return;
    search_ = text;
    applyView(lastView_);   // one model update per call
}

void StoreController::select(const QString& appId) {
    const AppRow* r = model_.find(appId);
    if (!r) return;
    QVariantMap d;
    d[QStringLiteral("appId")] = q(r->id);
    d[QStringLiteral("name")] = q(r->name);
    d[QStringLiteral("stateWords")] = QString::fromLatin1(pbstore::store::state_words(r->state));
    d[QStringLiteral("stateIcon")] = QString::fromLatin1(pbstore::store::state_icon(r->state));
    d[QStringLiteral("installedVersion")] = q(r->installed_version);
    d[QStringLiteral("offeredVersion")] = q(r->offered_version);
    QStringList notes;
    for (const std::string& n : r->notes) notes << q(n);
    d[QStringLiteral("notes")] = notes.join(QStringLiteral("; "));
    d[QStringLiteral("shortDescription")] = q(r->short_description);
    d[QStringLiteral("longDescription")] = q(r->long_description);
    d[QStringLiteral("license")] = q(r->license);
    d[QStringLiteral("author")] = q(r->author);
    d[QStringLiteral("sourceUrl")] = q(r->source_url);
    d[QStringLiteral("changelogUrl")] = q(r->changelog_url);
    d[QStringLiteral("minFirmware")] = q(r->min_firmware);
    d[QStringLiteral("packageUrl")] = q(r->package_url);
    d[QStringLiteral("packageSize")] = sizeText(r->package_size);
    d[QStringLiteral("inCatalog")] = r->in_catalog;
    d[QStringLiteral("isStore")] = r->is_store;
    d[QStringLiteral("storeNote")] = r->is_store ? tr("PB Installer on a computer can remove PB Store.") : QString();
    QStringList actions;
    for (pbstore::store::RowAction a : r->actions) actions << QString::fromLatin1(pbstore::store::action_words(a));
    d[QStringLiteral("actions")] = actions;
    detail_ = d;
    emit detailChanged();
}

void StoreController::refresh() {
    if (busy_) return;   // one operation at a time (spec FR-024)
    if (worker_.joinable()) worker_.join();
    clearMessage();
    setBusy(true);
    progress_ = tr("Refreshing the catalog");
    emit progressChanged();
    cancel_ = std::make_unique<pbstore::install::CancelToken>();   // a fresh token for each operation
    pageKind(QStringLiteral("progress"));
    worker_ = std::thread([this] {
        auto progress = [this](const pbstore::install::Progress& p) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastProgressMs_ < 2000) return;   // at most once every 2 seconds (spec FR-017)
            lastProgressMs_ = now;
            QString text = q(p.text);
            if (p.total > 0) text += QStringLiteral(" (%1 of %2 KiB)").arg(p.done / 1024).arg(p.total / 1024);
            else if (p.done > 0) text += QStringLiteral(" (%1 KiB)").arg(p.done / 1024);
            QMetaObject::invokeMethod(this, [this, text] { progress_ = text; emit progressChanged(); }, Qt::QueuedConnection);
        };
        pbstore::store::StoreOutcome out = service_.refresh(*cancel_, progress);
        if (leaving_) return;
        QMetaObject::invokeMethod(this, [this, out] {
            applyView(out.view);
            addNotices(out.notices);
            const auto messages = out.messages();
            if (!out.ok && !messages.empty()) showMessage(messages.front());
            progress_.clear();
            emit progressChanged();
            setBusy(false);
            pageKind(QStringLiteral("list"));
        }, Qt::QueuedConnection);
    });
}

void StoreController::loadSettings() {
    const pbstore::store::SettingsFacts f = service_.settings_facts();
    QVariantMap s;
    s[QStringLiteral("catalogUrl")] = q(f.catalog_url);
    s[QStringLiteral("refreshedAt")] = f.refreshed_at.empty() ? tr("never") : q(f.refreshed_at);
    s[QStringLiteral("generatedAt")] = f.generated_at.empty() ? tr("never") : q(f.generated_at);
    s[QStringLiteral("trustSource")] = q(f.trust_source);
    s[QStringLiteral("customCa")] = q(f.custom_ca);
    s[QStringLiteral("logLocation")] = q(f.log_location);
    s[QStringLiteral("battery")] = f.battery_percent ? QStringLiteral("%1 %").arg(*f.battery_percent) : tr("unknown");
    s[QStringLiteral("freeSpace")] = f.free_bytes ? QStringLiteral("%1 MiB").arg(*f.free_bytes / (1024 * 1024)) : tr("unknown");
    settings_ = s;
    emit settingsChanged();
}

void StoreController::loadLog() {
    // the last 50 lines of the newest log file; the file holds no personal data (spec FR-039)
    QFile f(logPath_);
    QStringList lines;
    if (f.open(QIODevice::ReadOnly)) {
        const QString all = QString::fromUtf8(f.readAll());
        lines = all.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    if (lines.size() > 50) lines = lines.mid(lines.size() - 50);
    log_ = lines.isEmpty() ? tr("The log is empty.") : lines.join(QLatin1Char('\n'));
    emit logChanged();
}

void StoreController::leave(int reason) {
    leaving_ = true;
    service_.request_leave(static_cast<pbstore::store::LeaveReason>(reason));
}

void StoreController::resumed() { leaving_ = false; }

void StoreController::cancel() { cancel_->cancel(); }
