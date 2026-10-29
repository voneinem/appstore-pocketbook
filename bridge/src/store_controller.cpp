#include "store_controller.h"

#include <QDateTime>
#include <QFile>
#include <QMetaObject>
#include <QStringList>

#include "pbstore/store/plan_text.h"
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
    s[QStringLiteral("readerIdentity")] = q(f.reader_identity);
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

void StoreController::cancel() { if (cancel_) cancel_->cancel(); }

// ---- the operations ---------------------------------------------------------------------------------------------------

void StoreController::async(std::function<void()> work, std::function<void()> done) {
    if (worker_.joinable()) worker_.join();
    worker_ = std::thread([this, work = std::move(work), done = std::move(done)]() mutable {
        work();
        if (leaving_) return;
        QMetaObject::invokeMethod(this, std::move(done), Qt::QueuedConnection);
    });
}

pbstore::install::ProgressFn StoreController::operationProgress() {
    return [this](const pbstore::install::Progress& p) {
        // after the commit point nothing can be cancelled: the engine's last phase is "finishing"
        if (p.phase == "finishing") QMetaObject::invokeMethod(this, [this] { cancellable_ = false; emit operationChanged(); }, Qt::QueuedConnection);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastProgressMs_ < 2000) return;   // at most once every 2 seconds (spec FR-017)
        lastProgressMs_ = now;
        QString text = q(p.text);
        if (p.total > 0) text += QStringLiteral(" (%1 of %2 %3)").arg(p.done).arg(p.total).arg(q(p.unit));
        else if (p.done > 0) text += QStringLiteral(" (%1 %2)").arg(p.done).arg(q(p.unit));
        QMetaObject::invokeMethod(this, [this, text] { progress_ = text; emit progressChanged(); }, Qt::QueuedConnection);
    };
}

void StoreController::setPhase(const QString& phase) {
    phase_ = phase;
    pageKind(phase == QLatin1String("planning") || phase == QLatin1String("executing") ? QStringLiteral("progress")
             : phase == QLatin1String("plan") ? QStringLiteral("plan") : phase == QLatin1String("result") ? QStringLiteral("result") : QStringLiteral("list"));
    emit operationChanged();
}

void StoreController::showFailure(const std::vector<pbstore::install::Message>& messages) {
    resultOk_ = false;
    resultText_ = messages.empty() ? tr("The action did not work.") : q(messages.front().what);
    resultReader_ = messages.empty() ? QString() : q(messages.front().reader_effect);
    resultTodo_ = messages.empty() ? QString() : q(messages.front().next);
    dataFolderLeft_ = false;
    deleteStep_ = 0;
}

void StoreController::showPlan(const pbstore::store::PlanView& p) {
    const pbstore::store::PlanTextParts parts = pbstore::store::split_plan_files(p.text);
    planFull_ = q(parts.full);
    planBrief_ = q(parts.brief);
    planFileLines_ = static_cast<int>(parts.file_lines);
    planFilesShown_ = false;
    extra_ = q(p.extra);
    nothingToDo_ = p.nothing_to_do;
    batteryUnreadable_ = p.battery_unreadable;
    confirmations_.clear();
    for (const auto& c : p.confirmations) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), q(c.name));
        m.insert(QStringLiteral("label"), q(c.label));
        confirmations_.append(m);
    }
    QStringList notices;
    for (const pbstore::Finding& n : p.notices)
        if (n.rule_id != "W-INS-03") notices << q(pbstore::store::store_message(n).what);
    notice_ = notices.join(QLatin1Char('\n'));
    emit noticeChanged();
}

void StoreController::startAction(const QString& action, const QString& appId) {
    if (busy_ || phase_ != QLatin1String("idle")) return;   // one operation at a time (spec FR-024)
    pbstore::store::RowAction a = pbstore::store::RowAction::Install;
    if (action == QLatin1String("Update")) a = pbstore::store::RowAction::Update;
    else if (action == QLatin1String("Remove")) a = pbstore::store::RowAction::Remove;
    else if (action == QLatin1String("Check") || action == QLatin1String("Adopt")) a = pbstore::store::RowAction::Check;
    else if (action == QLatin1String("Replace")) a = pbstore::store::RowAction::Replace;
    const AppRow* row = model_.find(appId);
    action_ = action;
    appId_ = appId;
    appName_ = row ? q(row->name) : appId;
    if (!updateAll_) title_ = (a == pbstore::store::RowAction::Check ? tr("Adopt") : action) + QLatin1Char(' ') + appName_;
    else title_ = tr("Updating %1 of %2: %3").arg(queuePos_ + 1).arg(queue_.size()).arg(appName_);
    clearMessage();
    cancel_ = std::make_unique<pbstore::install::CancelToken>();
    progress_ = tr("Preparing");
    emit progressChanged();
    cancellable_ = true;
    setBusy(true);
    setPhase(QStringLiteral("planning"));
    auto view = std::make_shared<pbstore::store::PlanView>();
    async([this, a, appId, view] { *view = service_.plan_action(a, appId.toStdString(), *cancel_, operationProgress()); },
          [this, view] {
              setBusy(false);
              if (!view->ok && view->findings.empty()) { setPhase(QStringLiteral("idle")); return; }   // ignored: another operation was running
              if (!view->ok) {
                  showFailure(view->messages());
                  if (updateAll_) { failed_ << appName_; updateAllSummary(); return; }
                  setPhase(QStringLiteral("result"));
                  return;
              }
              if (view->nothing_to_do) {   // W-INS-01: already at the offered version
                  resultOk_ = true;
                  resultText_ = tr("%1 is already at the offered version. Nothing was changed.").arg(appName_);
                  resultReader_.clear();
                  resultTodo_.clear();
                  if (updateAll_) { continueUpdateAll(); return; }
                  setPhase(QStringLiteral("result"));
                  return;
              }
              showPlan(*view);
              setPhase(QStringLiteral("plan"));
          });
}

void StoreController::decline() {
    service_.discard_plan();
    if (updateAll_) {   // declining one app skips it and goes on to the next
        ++queuePos_;
        continueUpdateAll();
        return;
    }
    setPhase(QStringLiteral("idle"));
    applyView(service_.list_cached());
}

void StoreController::confirm(const QStringList& given, bool acceptUnknownBattery) {
    if (busy_ || phase_ != QLatin1String("plan")) return;
    std::vector<std::string> names;
    for (const QString& g : given) names.push_back(g.toStdString());
    cancel_ = std::make_unique<pbstore::install::CancelToken>();
    progress_ = tr("Working");
    emit progressChanged();
    cancellable_ = true;
    setBusy(true);
    setPhase(QStringLiteral("executing"));
    auto view = std::make_shared<pbstore::store::ExecView>();
    async([this, names, acceptUnknownBattery, view] { *view = service_.execute_plan(names, acceptUnknownBattery, *cancel_, operationProgress()); },
          [this, view] {
              setBusy(false);
              applyView(view->view);
              if (view->ok) {
                  if (updateAll_) { updated_ << appName_; ++queuePos_; continueUpdateAll(); return; }
                  resultOk_ = true;
                  resultText_ = q(view->text);
                  resultReader_.clear();
                  resultTodo_.clear();
                  dataFolderLeft_ = view->data_folder_left;
                  deleteStep_ = 0;
                  setPhase(QStringLiteral("result"));
                  return;
              }
              showFailure(view->messages());
              if (updateAll_) { failed_ << appName_; updateAllSummary(); return; }
              setPhase(QStringLiteral("result"));
          });
}

void StoreController::finishResult() {
    updateAll_ = false;
    dataFolderLeft_ = false;
    deleteStep_ = 0;
    setPhase(QStringLiteral("idle"));
    applyView(service_.list_cached());
}

void StoreController::askDeleteData() {
    if (!dataFolderLeft_ || phase_ != QLatin1String("result")) return;
    const auto plan = service_.plan_delete_data();
    if (!plan.ok) { showFailure(plan.messages()); emit operationChanged(); return; }
    dataFolderText_ = tr("%1: %2 files, %3 KiB").arg(q(plan.data_folder)).arg(plan.data_files).arg(plan.data_bytes / 1024);
    deleteStep_ = 1;   // the first of two confirmations, each naming the folder, the number of files and the size
    emit operationChanged();
}

void StoreController::confirmDeleteData() {
    if (deleteStep_ == 1) {
        deleteStep_ = 2;
        emit operationChanged();
        return;
    }
    if (deleteStep_ != 2) return;
    cancel_ = std::make_unique<pbstore::install::CancelToken>();
    setBusy(true);
    deleteStep_ = 0;
    setPhase(QStringLiteral("executing"));
    auto view = std::make_shared<pbstore::store::ExecView>();
    async([this, view] { *view = service_.execute_delete_data(*cancel_, operationProgress()); },
          [this, view] {
              setBusy(false);
              applyView(view->view);
              dataFolderLeft_ = false;
              if (view->ok) { resultOk_ = true; resultText_ = q(view->text); resultReader_.clear(); resultTodo_.clear(); }
              else showFailure(view->messages());
              setPhase(QStringLiteral("result"));
          });
}

// ---- update all (story 3) --------------------------------------------------------------------------------------------------

void StoreController::startUpdateAll() {
    if (busy_ || phase_ != QLatin1String("idle")) return;
    queue_ = service_.update_all_queue();
    if (queue_.empty()) return;
    queuePos_ = 0;
    updated_.clear();
    failed_.clear();
    updateAll_ = true;
    startAction(QStringLiteral("Update"), q(queue_[queuePos_]));
}

void StoreController::continueUpdateAll() {
    phase_ = QStringLiteral("idle");   // the plan of the next app follows at once
    if (queuePos_ >= queue_.size()) { updateAllSummary(); return; }
    startAction(QStringLiteral("Update"), q(queue_[queuePos_]));
}

void StoreController::updateAllSummary() {
    // the first failure stops the run: updated, failed, not tried
    QStringList notTried;
    const size_t next = failed_.isEmpty() ? queue_.size() : queuePos_ + 1;
    for (size_t i = next; i < queue_.size(); ++i) {
        const AppRow* row = model_.find(q(queue_[i]));
        notTried << (row ? q(row->name) : q(queue_[i]));
    }
    QStringList lines;
    lines << tr("Updated: %1").arg(updated_.isEmpty() ? tr("none") : updated_.join(QStringLiteral(", ")));
    if (!failed_.isEmpty()) lines << tr("Failed: %1").arg(failed_.join(QStringLiteral(", ")));
    if (!notTried.isEmpty()) lines << tr("Not tried: %1").arg(notTried.join(QStringLiteral(", ")));
    if (failed_.isEmpty() && updated_.isEmpty()) lines << tr("Nothing was updated.");
    const bool failed = !failed_.isEmpty();
    const QString reader = resultReader_, todo = resultTodo_, first = resultText_;
    resultOk_ = !failed;
    resultText_ = (failed ? first + QLatin1Char('\n') : QString()) + lines.join(QLatin1Char('\n'));
    resultReader_ = failed ? reader : QString();
    resultTodo_ = failed ? todo : QString();
    updateAll_ = false;
    title_ = tr("Update all");
    applyView(service_.list_cached());
    setPhase(QStringLiteral("result"));
}

// ---- repair (story 6) --------------------------------------------------------------------------------------------------------

QString StoreController::repairText() const {
    const auto s = service_.repair_status();
    if (!s.pending) return QString();
    return tr("An operation of %1 was interrupted (%2 of %3 to %4). PB Store can %5 it now. Until it is done, nothing can be installed, updated or removed. The app named may be incomplete or unusable until it is repaired.")
        .arg(q(s.app_id.empty() ? std::string("an app") : s.app_id), q(s.kind), q(s.from_version.empty() ? std::string("nothing") : s.from_version),
             q(s.to_version.empty() ? std::string("nothing") : s.to_version), q(s.action == "finish" ? std::string("finish") : std::string("undo")));
}

void StoreController::startRepair(bool acceptUnknownBattery) {
    if (busy_) return;
    cancel_ = std::make_unique<pbstore::install::CancelToken>();
    title_ = tr("Repair");
    cancellable_ = false;
    setBusy(true);
    setPhase(QStringLiteral("executing"));
    auto view = std::make_shared<pbstore::store::ExecView>();
    async([this, acceptUnknownBattery, view] { *view = service_.repair(acceptUnknownBattery, *cancel_, operationProgress()); },
          [this, view] {
              setBusy(false);
              repairPending_ = service_.repair_status().pending;
              applyView(view->view);
              if (view->ok) { resultOk_ = true; resultText_ = q(view->text); resultReader_.clear(); resultTodo_.clear(); notice_.clear(); emit noticeChanged(); }
              else showFailure(view->messages());
              setPhase(QStringLiteral("result"));
          });
}
