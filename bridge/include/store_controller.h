#pragma once
// The Qt layer over the Qt-free StoreService (research R-16). It only adapts: properties and models for QML, a worker thread for the
// blocking Refresh, the throttling of progress (at most once every 2 seconds, spec FR-017), the three-part messages. It makes no
// decision of its own (constitution Principle VI, scripts/check-constitution.sh R19).

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <thread>

#include "app_list_model.h"
#include "pbstore/store/refresh_policy.h"
#include "pbstore/store/screen.h"
#include "pbstore/store/store_service.h"

class StoreController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString catalogLine READ catalogLine NOTIFY viewChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(bool messageVisible READ messageVisible NOTIFY messageChanged)
    Q_PROPERTY(QString messageTitle READ messageTitle NOTIFY messageChanged)
    Q_PROPERTY(QString messageReader READ messageReader NOTIFY messageChanged)
    Q_PROPERTY(QString messageTodo READ messageTodo NOTIFY messageChanged)
    Q_PROPERTY(AppListModel* model READ model CONSTANT)
    Q_PROPERTY(QString searchText READ searchText NOTIFY viewChanged)
    Q_PROPERTY(QString searchSummary READ searchSummary NOTIFY viewChanged)
    Q_PROPERTY(bool repairPending READ repairPending NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    StoreController(pbstore::store::StoreService& service, pbstore::store::Screen& screen, const QString& version, const QString& logPath, QObject* parent = nullptr);
    ~StoreController() override;

    bool busy() const { return busy_; }
    QString catalogLine() const;
    QString notice() const { return notice_; }
    QString progressText() const { return progress_; }
    bool messageVisible() const { return !messageTitle_.isEmpty(); }
    QString messageTitle() const { return messageTitle_; }
    QString messageReader() const { return messageReader_; }
    QString messageTodo() const { return messageTodo_; }
    AppListModel* model() { return &model_; }
    QString searchText() const { return search_; }
    QString searchSummary() const;
    bool repairPending() const { return repairPending_; }
    QVariantMap detail() const { return detail_; }
    QVariantMap settings() const { return settings_; }
    QString logText() const { return log_; }
    QString version() const { return version_; }

    // At start, before the list: the cancel note, the launcher's message, an interrupted operation; then the list from the cache.
    Q_INVOKABLE void startUp();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void search(const QString& text);
    Q_INVOKABLE void select(const QString& appId);
    Q_INVOKABLE void clearMessage();
    Q_INVOKABLE void loadSettings();
    Q_INVOKABLE void loadLog();
    // The page kind changed ("list", "plan", "progress", "result", "other") or the user turned a page: the policy decides on a full refresh.
    Q_INVOKABLE void pageKind(const QString& kind);
    Q_INVOKABLE void pageTurned();
    // The first screen is shown: the start counts as successful (spec FR-034).
    Q_INVOKABLE void markStarted();
    Q_INVOKABLE void leave(int reason);
    Q_INVOKABLE void resumed();                // the app came back after a sleep: a new action may start
    Q_INVOKABLE void cancel();                 // the Cancel control: stops a refresh at once

signals:
    void busyChanged();
    void viewChanged();
    void noticeChanged();
    void progressChanged();
    void messageChanged();
    void detailChanged();
    void settingsChanged();
    void logChanged();

private:
    void applyView(const pbstore::store::ListView& view);
    void showMessage(const pbstore::install::Message& m);
    void addNotices(const std::vector<pbstore::Finding>& notices);
    void fullRefresh(bool wanted);
    void setBusy(bool b);

    pbstore::store::StoreService& service_;
    pbstore::store::Screen& screen_;
    pbstore::store::RefreshPolicy policy_;
    AppListModel model_;
    QString version_, logPath_;
    QString catalogLine_, notice_, progress_, search_, summary_;
    QString messageTitle_, messageReader_, messageTodo_, log_;
    QVariantMap detail_, settings_;
    bool busy_ = false;
    bool repairPending_ = false;
    bool catalogLoaded_ = false;
    qint64 lastProgressMs_ = 0;
    std::thread worker_;
    std::atomic<bool> leaving_{false};
    std::unique_ptr<pbstore::install::CancelToken> cancel_ = std::make_unique<pbstore::install::CancelToken>();
    pbstore::store::ListView lastView_;
    pbstore::store::StartResult startResult_;
};
