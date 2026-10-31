#pragma once
// The Qt layer over the Qt-free StoreService (research R-16). It only adapts: properties and models for QML, a worker thread for the
// blocking Refresh, the throttling of progress (at most once every 2 seconds, spec FR-017), the three-part messages. It makes no
// decision of its own (constitution Principle VI, scripts/check-constitution.sh R19).

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

#include "app_list_model.h"
#include "pbstore/store/about.h"
#include "pbstore/store/progress_text.h"
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
    Q_PROPERTY(QString aboutNotice READ aboutNotice CONSTANT)
    Q_PROPERTY(QVariantList licenses READ licenses NOTIFY licensesChanged)          // the components of licenses/manifest.txt, in file order
    Q_PROPERTY(QVariantMap appLicense READ appLicense NOTIFY licensesChanged)       // the store's own entry: name, license, copyright
    Q_PROPERTY(bool licensesAvailable READ licensesAvailable NOTIFY licensesChanged)
    Q_PROPERTY(QString licenseTitle READ licenseTitle NOTIFY licenseChanged)        // the entry that loadLicense opened
    Q_PROPERTY(QString licenseText READ licenseText NOTIFY licenseChanged)
    Q_PROPERTY(bool licenseAvailable READ licenseAvailable NOTIFY licenseChanged)
    // the operation: idle, planning, plan, executing, result (one at a time)
    Q_PROPERTY(QString phase READ phase NOTIFY operationChanged)
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY operationChanged)
    Q_PROPERTY(QString planText READ planText NOTIFY operationChanged)
    Q_PROPERTY(QString planSummary READ planSummary NOTIFY operationChanged)         // the plain sentences shown first
    Q_PROPERTY(bool planDetailsShown READ planDetailsShown NOTIFY operationChanged)  // planText (the installer's full text) is behind "Show details"
    Q_PROPERTY(QVariantList confirmations READ confirmations NOTIFY operationChanged)
    Q_PROPERTY(bool batteryUnreadable READ batteryUnreadable NOTIFY operationChanged)
    Q_PROPERTY(bool nothingToDo READ nothingToDo NOTIFY operationChanged)
    Q_PROPERTY(QString extraLine READ extraLine NOTIFY operationChanged)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY operationChanged)
    Q_PROPERTY(bool resultOk READ resultOk NOTIFY operationChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY operationChanged)
    Q_PROPERTY(QString resultReader READ resultReader NOTIFY operationChanged)
    Q_PROPERTY(QString resultTodo READ resultTodo NOTIFY operationChanged)
    Q_PROPERTY(bool canDeleteData READ canDeleteData NOTIFY operationChanged)
    Q_PROPERTY(int deleteStep READ deleteStep NOTIFY operationChanged)
    Q_PROPERTY(QString dataFolderText READ dataFolderText NOTIFY operationChanged)
    Q_PROPERTY(bool canUpdateAll READ canUpdateAll NOTIFY viewChanged)
    Q_PROPERTY(QString repairText READ repairText NOTIFY viewChanged)

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
    QString aboutNotice() const;
    // Where the manifest and the license texts are read from: `:/licenses` (compiled into the program) by default, a folder in tests.
    void setLicensesRoot(const QString& root);
    QVariantList licenses();
    QVariantMap appLicense();
    bool licensesAvailable();
    QString licenseTitle() const { return licenseTitle_; }
    QString licenseText() const { return licenseText_; }
    bool licenseAvailable() const { return licenseAvailable_; }
    QString phase() const { return phase_; }
    QString operationTitle() const { return title_; }
    QString planSummary() const { return planSummary_; }
    QString planText() const { return planText_; }
    bool planDetailsShown() const { return planDetailsShown_; }
    QVariantList confirmations() const { return confirmations_; }
    bool batteryUnreadable() const { return batteryUnreadable_; }
    bool nothingToDo() const { return nothingToDo_; }
    QString extraLine() const { return extra_; }
    bool cancellable() const { return cancellable_; }
    bool resultOk() const { return resultOk_; }
    QString resultText() const { return resultText_; }
    QString resultReader() const { return resultReader_; }
    QString resultTodo() const { return resultTodo_; }
    bool canDeleteData() const { return dataFolderLeft_; }
    int deleteStep() const { return deleteStep_; }
    QString dataFolderText() const { return dataFolderText_; }
    bool canUpdateAll() const { return !service_.update_all_queue().empty() && !repairPending_; }
    QString repairText() const;

    // At start, before the list: the cancel note, the launcher's message, an interrupted operation; then the list from the cache.
    Q_INVOKABLE void startUp();
    Q_INVOKABLE void toggleDetails() { planDetailsShown_ = !planDetailsShown_; emit operationChanged(); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void search(const QString& text);
    Q_INVOKABLE void select(const QString& appId);
    Q_INVOKABLE void clearMessage();
    Q_INVOKABLE void loadSettings();
    Q_INVOKABLE void loadLog();
    Q_INVOKABLE void loadLicense(const QString& id);   // opens the text of one entry of the manifest (the store's own license included)
    // The page kind changed ("list", "plan", "progress", "result", "other") or the user turned a page: the policy decides on a full refresh.
    Q_INVOKABLE void pageKind(const QString& kind);
    Q_INVOKABLE void pageTurned();
    // The first screen is shown: the start counts as successful (spec FR-034).
    Q_INVOKABLE void markStarted();
    Q_INVOKABLE void leave(int reason);
    // The operations (stories 2 to 6). `action` is the word of the button: Install, Update, Remove, Check.
    Q_INVOKABLE void startAction(const QString& action, const QString& appId);
    Q_INVOKABLE void confirm(const QStringList& given, bool acceptUnknownBattery);   // the Confirm button of the plan
    Q_INVOKABLE void decline();                                                       // the plan was not confirmed: nothing is written
    Q_INVOKABLE void finishResult();                                                  // OK on the result
    Q_INVOKABLE void askDeleteData();                                                 // "Delete data too?" after a removal
    Q_INVOKABLE void confirmDeleteData();                                             // each of the two confirmations
    Q_INVOKABLE void startUpdateAll();
    Q_INVOKABLE void startRepair(bool acceptUnknownBattery);
    Q_INVOKABLE void leaveAndWait();           // the house icon: stop what runs and wait until its worker has ended (Wi-Fi back as it was), before the program is closed
    Q_INVOKABLE void resumed();                // the app came back after a sleep: a new action may start
    Q_INVOKABLE void cancel();                 // the Cancel control: stops a refresh at once

signals:
    void busyChanged();
    void viewChanged();
    void operationChanged();
    void noticeChanged();
    void progressChanged();
    void messageChanged();
    void detailChanged();
    void settingsChanged();
    void logChanged();
    void licensesChanged();
    void licenseChanged();

private:
    void applyView(const pbstore::store::ListView& view);
    void showMessage(const pbstore::install::Message& m);
    void addNotices(const std::vector<pbstore::Finding>& notices);
    void fullRefresh(bool wanted);
    void setBusy(bool b);
    void async(std::function<void()> work, std::function<void()> done);
    pbstore::install::ProgressFn operationProgress();
    void setPhase(const QString& phase);
    void showPlan(const pbstore::store::PlanView& p);
    void showResult(const pbstore::store::ExecView& e);
    void showFailure(const std::vector<pbstore::install::Message>& messages);
    void continueUpdateAll();
    void updateAllSummary();

    pbstore::store::StoreService& service_;
    pbstore::store::Screen& screen_;
    pbstore::store::RefreshPolicy policy_;
    AppListModel model_;
    QString version_, logPath_;
    QString catalogLine_, notice_, progress_, search_, summary_;
    QString messageTitle_, messageReader_, messageTodo_, log_;
    QVariantMap detail_, settings_;
    QString licensesRoot_ = QStringLiteral(":/licenses");
    std::optional<pbstore::store::about::Manifest> manifest_;
    QString licenseTitle_, licenseText_;
    bool licenseAvailable_ = false;
    const pbstore::store::about::Manifest& manifest();
    bool busy_ = false;
    bool repairPending_ = false;
    bool catalogLoaded_ = false;
    pbstore::store::ProgressThrottle progressThrottle_;
    std::atomic<bool> reportDropped_{false};   // a worker ended without reporting because the store was being left (see resumed())
    std::thread worker_;
    std::atomic<bool> leaving_{false};
    std::unique_ptr<pbstore::install::CancelToken> cancel_ = std::make_unique<pbstore::install::CancelToken>();
    pbstore::store::ListView lastView_;
    pbstore::store::StartResult startResult_;
    // the operation
    QString planSummary_, planText_;
    bool planDetailsShown_ = false;
    QString phase_ = QStringLiteral("idle"), title_, extra_, resultText_, resultReader_, resultTodo_, dataFolderText_;
    QVariantList confirmations_;
    bool batteryUnreadable_ = false, nothingToDo_ = false, cancellable_ = false, resultOk_ = false, dataFolderLeft_ = false;
    int deleteStep_ = 0;
    QString action_, appId_, appName_;
    // "Update all": the queue and what happened to each app
    std::vector<std::string> queue_;
    size_t queuePos_ = 0;
    QStringList updated_, failed_;
    bool updateAll_ = false;
};
