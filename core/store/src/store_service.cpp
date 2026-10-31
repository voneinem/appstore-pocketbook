#include "pbstore/store/store_service.h"

#include <chrono>
#include <thread>

#include "pbstore/catalog.h"
#include "pbstore/install/text_safety.h"
#include "pbstore/semver.h"
#include "pbstore/store/clock_check.h"
#include "pbstore/store/memory_gate.h"
#include "pbstore/store/plan_summary.h"
#include "pbstore/store/power_gate.h"
#include "pbstore/store/app_filter.h"
#include "pbstore/store/self_update_view.h"
#include "pbstore/store/store_rules.h"

namespace pbstore {
namespace store {

std::vector<install::Message> StoreOutcome::messages() const {
    std::vector<install::Message> out;
    for (const Finding& f : findings) out.push_back(store_message(f));
    return out;
}

std::vector<install::Message> PlanView::messages() const {
    std::vector<install::Message> out;
    for (const Finding& f : findings) out.push_back(store_message(f));
    return out;
}

std::vector<install::Message> ExecView::messages() const {
    std::vector<install::Message> out;
    for (const Finding& f : findings) out.push_back(store_message(f, effect));
    return out;
}

std::vector<install::Message> StoreOutcome::notice_messages() const {
    std::vector<install::Message> out;
    for (const Finding& f : notices) out.push_back(store_message(f));
    return out;
}

StoreService::StoreService(StoreParts parts, StoreConfig config) : parts_(std::move(parts)), config_(std::move(config)) {
    if (!config_.sleep_ms) config_.sleep_ms = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    if (!config_.now_ms) config_.now_ms = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    };
}

std::shared_ptr<install::ReaderHandle> StoreService::reader() {
    if (reader_) return reader_;
    auto opened = parts_.engine.open_reader(std::optional<std::string>(parts_.storage.root));
    if (opened.ok()) reader_ = *opened.value;
    return reader_;
}

// Builds the rows from the engine's listing and the verified catalog the engine used (read back from the cache, whose bytes the
// engine verified; the store does not verify anything itself).
ListView StoreService::view_from(const install::Listing& listing, std::vector<Finding> notices) {
    ListView v;
    v.notices = std::move(notices);
    RowContext ctx;
    if (listing.catalog_loaded) {
        std::string bytes, signature;
        if (parts_.cache.load(&bytes, &signature)) ctx.catalog = parse_catalog(bytes).catalog;
    }
    // PB Store's own note: the version that runs against the version in its install record
    SelfUpdateInputs in;
    in.running_version = config_.running_version;
    for (const install::AppEntry& e : listing.apps)
        if (e.id == kStoreAppId) in.record_version = e.installed_version;
    in.message = launcher_message_;
    in.failed_present = parts_.run_state.failed_present();
    in.last_run_version = parts_.run_state.last_run_version();
    ctx.self_update = self_update_view(in);
    ctx.needs_repair = needs_repair_;
    v.rows = build_rows(listing.apps, ctx);
    if (repair_pending_) {   // nothing can be started until the repair is done; the rows say so
        for (AppRow& r : v.rows) {
            r.actions.clear();
            r.notes.insert(r.notes.begin(), "repair pending");
        }
    }
    v.catalog.loaded = listing.catalog_loaded;
    v.catalog.generated_at = listing.catalog_generated_at;
    if (const auto refreshed = parts_.cache.refreshed_at()) v.catalog.refreshed_at = *refreshed;
    if (!listing.catalog_loaded) v.catalog.refreshed_at.clear();
    last_view_ = v;
    return v;
}

ListView StoreService::list_cached() {
    auto handle = reader();
    if (!handle) {
        ListView empty;
        empty.notices.push_back(make_store_finding("E-INS-03", "reader", "the reader's storage is not in the expected shape"));
        return empty;
    }
    std::vector<Finding> notices;
    bool discarded = false;
    auto cached = parts_.engine.list_cached(*handle);
    if (!cached.ok()) {
        // the cached catalog failed its check on load: the engine discarded it; the list falls back to the apps on the reader
        discarded = true;
        notices.push_back(make_store_finding("E-STO-09", "catalog", "The saved catalog failed its check and was discarded."));
        auto installed = parts_.engine.installed(*handle);
        ListView v = view_from(installed.value ? *installed.value : install::Listing(), std::move(notices));
        v.catalog.discarded = discarded;
        last_view_ = v;
        return v;
    }
    for (const Finding& n : cached.notices)
        if (n.message.find("no cached catalog") == std::string::npos) notices.push_back(n);   // "no catalog yet" is the view's own state
    return view_from(*cached.value, std::move(notices));
}

std::vector<Finding> StoreService::adapt(std::vector<Finding> findings) {
    for (Finding& f : findings) {
        if (f.rule_id != "E-INS-26" || parts_.last_fetch->failure() != FetchKind::Tls) continue;
        std::optional<std::string> generated;
        if (last_view_.catalog.loaded && !last_view_.catalog.generated_at.empty()) generated = last_view_.catalog.generated_at;
        const install::Utc now = parts_.clock.now();
        if (clock_looks_wrong(now, config_.release_date, generated, FetchKind::Tls))
            f = make_store_finding("E-STO-04", "clock",
                                   "The reader's clock looks wrong: it says " + clock_date_text(now) + ", so the certificate of the server cannot be checked.");
    }
    return findings;
}

namespace {

// While an operation runs, a request to leave cancels its token. If the request came first, the token is cancelled at once.
class ActiveCancel {
public:
    ActiveCancel(std::atomic<install::CancelToken*>& slot, const std::atomic<bool>& leave, install::CancelToken& token) : slot_(slot) {
        slot_ = &token;
        if (leave.load()) token.cancel();
    }
    ~ActiveCancel() { slot_ = nullptr; }
    ActiveCancel(const ActiveCancel&) = delete;
    ActiveCancel& operator=(const ActiveCancel&) = delete;

private:
    std::atomic<install::CancelToken*>& slot_;
};

}  // namespace

StoreOutcome StoreService::refresh(install::CancelToken& cancel, const install::ProgressFn& progress) {
    StoreOutcome out;
    if (leave_requested_.load()) return out;   // the user left: nothing is started (spec FR-025)
    parts_.last_fetch->clear();
    ActiveCancel active(active_cancel_, leave_requested_, cancel);
    {
        RadioSession radio(parts_.radio, config_.sleep_ms, config_.now_ms);
        auto cancelled = [&cancel] { return cancel.cancelled(); };
        if (auto f = radio.begin(cancelled)) {
            out.findings.push_back(*f);
        } else {
            // the serial memory is the larger of state.json and the cache: raise a missing or damaged state.json first, so that the
            // engine's rollback rule uses it
            parts_.cache.restore_serial_memory(trusted_keys());
            auto handle = reader();
            if (!handle) {
                out.findings.push_back(make_store_finding("E-INS-03", "reader", "the reader's storage is not in the expected shape"));
            } else {
                auto listed = parts_.engine.list(*handle, progress, cancel);
                out.notices = listed.notices;
                if (listed.ok()) {
                    out.ok = true;
                    out.view = view_from(*listed.value, {});
                } else {
                    out.findings = adapt(listed.findings);
                }
            }
        }
        if (auto notice = radio.end()) out.notices.push_back(*notice);
    }
    if (!out.ok) {
        ListView cached = list_cached();                  // after a failure the list still comes from the cache
        out.view = cached;
        for (const Finding& n : cached.notices) out.notices.push_back(n);
    }
    return out;
}

std::vector<AppRow> StoreService::rows(const std::string& filter) const { return filter_apps(last_view_.rows, filter); }

SettingsFacts StoreService::settings_facts() {
    SettingsFacts f;
    f.catalog_url = config_.catalog_url;
    f.trust_source = config_.trust_source;
    f.log_location = "system/pbstore/app/pbstore.log";
    if (config_.reader_identity) f.reader_identity = config_.reader_identity();
    if (const auto refreshed = parts_.cache.refreshed_at()) f.refreshed_at = *refreshed;
    f.generated_at = last_view_.catalog.generated_at;
    f.battery_percent = parts_.battery.percent();
    if (auto handle = reader()) {
        const install::Space s = handle->fs->space();
        f.free_bytes = s.free_bytes;
    }
    return f;
}

StartResult StoreService::start_check() {
    StartResult r;
    auto handle = reader();
    bool journal = false;
    if (handle) {
        auto installed = parts_.engine.installed(*handle);
        if (installed.ok() && installed.value->interrupted) {
            journal = true;
            r.repair = installed.value->interrupted;
        }
    }
    r.repair_pending = journal;
    repair_pending_ = journal;
    repair_status_ = RepairStatus();
    if (journal && r.repair) {
        repair_status_.pending = true;
        repair_status_.text = r.repair->text();
        repair_status_.kind = r.repair->kind;
        repair_status_.app_id = r.repair->app_id;
        repair_status_.from_version = r.repair->from_version;
        repair_status_.to_version = r.repair->to_version;
        repair_status_.reason = r.repair->reason;
        repair_status_.action = r.repair->action == install::RepairPlan::Action::Finish ? "finish" : "undo";
    }
    if (auto note = parts_.pending_note.at_start(journal)) r.notices.push_back(*note);
    // what the launcher left: shown once
    launcher_message_ = parts_.run_state.take_message();
    switch (launcher_message_.kind) {
        case RunMessage::Kind::UpdateFailed:
            r.notices.push_back(make_store_finding("E-STO-06", "pbstore", "Version " + launcher_message_.detail + " of PB Store did not start; the previous version was restored."));
            break;
        case RunMessage::Kind::SwapSkipped:
            r.notices.push_back(make_store_finding("E-STO-07", "pbstore", "The new version of PB Store could not be put in place (" + install::neutralize(launcher_message_.detail) +
                                                                          "); the previous version keeps running."));
            break;
        case RunMessage::Kind::Restored:
        case RunMessage::Kind::Unknown:
        case RunMessage::Kind::None:
            break;
    }
    return r;
}

void StoreService::mark_started(const std::string& version) {
    parts_.run_state.mark_started();
    parts_.run_state.remember_run_version(version);
}

void StoreService::resume() { leave_requested_ = false; }

void StoreService::request_leave(LeaveReason) {
    leave_requested_ = true;
    if (install::CancelToken* c = active_cancel_.load()) c->cancel();   // a download stops and writes nothing; Wi-Fi is restored by the session
}

// ---- the write actions ------------------------------------------------------------------------------------------------

namespace {

// Holds the one operation slot for the length of a call (spec FR-024).
class Slot {
public:
    explicit Slot(std::atomic<bool>& flag) : flag_(flag) {
        bool expected = false;
        held_ = flag_.compare_exchange_strong(expected, true);
    }
    ~Slot() { if (held_) flag_ = false; }
    bool held() const { return held_; }

private:
    std::atomic<bool>& flag_;
    bool held_ = false;
};

install::Action::Type action_type(RowAction a) {
    switch (a) {
        case RowAction::Install: return install::Action::Type::Install;
        case RowAction::Update: return install::Action::Type::Update;
        case RowAction::Remove: return install::Action::Type::Remove;
        case RowAction::Check:
        case RowAction::Adopt: return install::Action::Type::Adopt;
        case RowAction::Replace: return install::Action::Type::Replace;
    }
    return install::Action::Type::Install;
}

const char* kind_name(RowAction a) {
    switch (a) {
        case RowAction::Install: return "install";
        case RowAction::Update: return "update";
        case RowAction::Remove: return "remove";
        case RowAction::Check:
        case RowAction::Adopt: return "adopt";
        case RowAction::Replace: return "replace";
    }
    return "";
}

PowerOperation power_op(RowAction a, bool is_store) {
    switch (a) {
        case RowAction::Install: return PowerOperation::Install;
        case RowAction::Update: return is_store ? PowerOperation::SelfUpdate : PowerOperation::Update;
        case RowAction::Remove: return PowerOperation::Remove;
        case RowAction::Check:
        case RowAction::Adopt: return PowerOperation::Adopt;
        case RowAction::Replace: return PowerOperation::Update;      // it writes the app's files like an update
    }
    return PowerOperation::Install;
}

std::string confirmation_label(install::Confirmation c) {
    switch (c) {
        case install::Confirmation::Plan: return "Carry out this plan";
        case install::Confirmation::EditedFiles: return "Files of the app were changed since the install (see the details); continue anyway";
        case install::Confirmation::UnknownFirmware: return "The firmware version of the reader is unknown; continue at my own risk";
        case install::Confirmation::UnknownModel: return "The model of the reader is unknown; continue at my own risk";
        case install::Confirmation::ExistingDataFolder: return "The data folder exists; its contents stay untouched; continue";
        case install::Confirmation::ReplaceOutside: return "Replace the app that was put on the reader by hand";
        case install::Confirmation::Repair: return "Finish or undo the interrupted operation";
        case install::Confirmation::DeleteData1: return "Delete the data folder named above";
        case install::Confirmation::DeleteData2: return "Delete the number of files named above";
    }
    return "";
}

}  // namespace

PlanView StoreService::plan_action(RowAction action, const std::string& app_id, install::CancelToken& cancel, const install::ProgressFn& progress) {
    PlanView v;
    v.kind = kind_name(action);
    v.app_id = app_id;
    Slot slot(operation_);
    if (!slot.held()) return v;   // one operation at a time: further taps are ignored
    if (leave_requested_.load()) return v;
    ActiveCancel active(active_cancel_, leave_requested_, cancel);
    plan_.reset();
    parts_.last_fetch->clear();
    const AppRow* row = nullptr;
    for (const AppRow& r : last_view_.rows) if (r.id == app_id) row = &r;
    v.app_name = row ? row->name : app_id;
    v.is_store = app_id == kStoreAppId;
    if (repair_pending_) {
        v.findings.push_back(make_error("E-INS-23", "reader", "an earlier operation on this reader was interrupted and has to be repaired first"));
        return v;
    }
    if (v.is_store && action == RowAction::Remove) {
        v.findings.push_back(make_store_finding("E-STO-08", "pbstore", "PB Store does not remove itself."));
        return v;
    }
    auto handle = reader();
    if (!handle) {
        v.findings.push_back(make_store_finding("E-INS-03", "reader", "the reader's storage is not in the expected shape"));
        return v;
    }
    // the battery, before anything is downloaded
    const PowerDecision power = decide_power(power_op(action, v.is_store), parts_.battery.percent(), parts_.battery.charging());
    if (power.result == PowerResult::Refused) {
        v.findings.push_back(power_finding(power).finding);
        return v;
    }
    v.battery_unreadable = power.result == PowerResult::NeedsRiskConfirmation;
    const bool downloads = action != RowAction::Remove;
    if (downloads && row && row->package_size > 0) {   // storage and memory, from the signed package size, before the download
        const MemoryDecision mem = decide_memory(static_cast<uint64_t>(row->package_size), handle->fs->space().free_bytes, parts_.memory.available_bytes());
        if (mem.result != MemoryResult::Ok) {
            v.findings.push_back(memory_finding(mem).finding);
            return v;
        }
    }
    install::Action engine_action;
    engine_action.type = action_type(action);
    engine_action.app_id = app_id;
    {
        RadioSession radio(parts_.radio, config_.sleep_ms, config_.now_ms);
        bool connected = true;
        if (downloads) {
            auto cancelled = [&cancel] { return cancel.cancelled(); };
            if (auto f = radio.begin(cancelled)) {
                v.findings.push_back(*f);
                connected = false;
            }
        }
        if (connected) {
            parts_.cache.restore_serial_memory(trusted_keys());
            auto planned = parts_.engine.plan(*handle, engine_action, progress, cancel);
            v.notices = planned.notices;
            if (planned.ok()) {
                plan_ = std::make_shared<install::PlanHandle>(*planned.value);
                v.ok = true;
                v.text = plan_->text;
                v.summary = plain_plan_summary(plan_->plan);
                v.nothing_to_do = plan_->nothing_to_do;
                if (v.app_name.empty() || v.app_name == app_id) v.app_name = plan_->plan.app_name;
                for (install::Confirmation c : plan_->plan.confirmations)
                    if (c != install::Confirmation::Plan) v.confirmations.push_back({install::confirmation_name(c), confirmation_label(c)});
                if (plan_->plan.data && plan_->plan.data->exists) {
                    v.data_folder = plan_->plan.data->path;
                    v.data_files = plan_->plan.data->files;
                    v.data_bytes = plan_->plan.data->bytes;
                }
                if (v.is_store && action == RowAction::Update) v.extra = "The update takes effect when you open PB Store again.";
            } else {
                v.findings = adapt(planned.findings);
            }
        }
        if (auto notice = radio.end()) v.notices.push_back(*notice);
    }
    return v;
}

void StoreService::discard_plan() {
    plan_.reset();
    delete_plan_.reset();
}

ExecView StoreService::execute_plan(const std::vector<std::string>& given_names, bool accept_unknown_battery, install::CancelToken& cancel, const install::ProgressFn& progress) {
    ExecView e;
    Slot slot(operation_);
    if (!slot.held()) return e;
    if (leave_requested_.load()) return e;
    ActiveCancel active(active_cancel_, leave_requested_, cancel);
    auto handle = reader();
    if (!plan_ || !handle) {
        e.findings.push_back(make_error("E-INS-25", "plan", "there is no plan to carry out; make a new plan"));
        return e;
    }
    const install::Plan& plan = plan_->plan;
    const bool is_store = plan.app_id == kStoreAppId;
    RowAction action = RowAction::Install;
    if (plan.kind == install::PlanKind::Update) action = RowAction::Update;
    else if (plan.kind == install::PlanKind::Remove) action = RowAction::Remove;
    else if (plan.kind == install::PlanKind::Adopt) action = RowAction::Adopt;
    else if (plan.kind == install::PlanKind::Replace) action = RowAction::Replace;
    // the battery again, immediately before the first write (spec FR-016)
    const PowerDecision power = decide_power(power_op(action, is_store), parts_.battery.percent(), parts_.battery.charging());
    if (power.result == PowerResult::Refused || (power.result == PowerResult::NeedsRiskConfirmation && !accept_unknown_battery)) {
        e.findings.push_back(power_finding(power).finding);
        plan_.reset();
        return e;
    }
    install::Confirmations given = {install::Confirmation::Plan};
    for (const std::string& n : given_names) {
        install::Confirmation c;
        if (install::confirmation_from_name(n, &c)) given.insert(c);
    }
    parts_.pending_note.write({kind_name(action), plan.app_id, plan.from_version, plan.to_version});
    install::ExecResult res = parts_.engine.execute(*handle, *plan_, given, progress, cancel);
    e.ok = res.ok();
    e.cancelled = res.cancelled;
    e.effect = res.effect;
    e.findings = res.findings;
    if (!res.missing_confirmations.empty() && e.findings.empty())
        e.findings.push_back(make_error("E-INS-25", "plan", "a confirmation that the plan names was not given"));
    if (e.ok || !leave_requested_.load()) parts_.pending_note.clear();   // after a leave that stopped the action the note stays, so that the next start says what happened
    if (res.outcome == install::ExecResult::Outcome::RepairNeeded) needs_repair_.insert(plan.app_id);
    if (e.ok) {
        WordingArgs args;
        args.app = plan.app_name.empty() ? plan.app_id : plan.app_name;
        args.group = plan.menu.group;
        if (plan.data && !plan.data->path.empty()) args.folder = plan.data->path;
        args.version = plan.to_version;
        WordingOp op = WordingOp::Install;
        if (action == RowAction::Update) op = is_store ? WordingOp::SelfUpdate : WordingOp::Update;
        else if (action == RowAction::Remove) op = WordingOp::Remove;
        else if (action == RowAction::Adopt) op = WordingOp::Adopt;
        e.text = result_text(config_.facts, op, args);
        if (action == RowAction::Remove && plan.data && plan.data->exists && !plan.delete_data) {
            e.data_folder_left = true;
            removal_plan_ = plan;
        } else {
            removal_plan_.reset();
        }
    }
    plan_.reset();
    e.view = list_cached();
    return e;
}

PlanView StoreService::plan_delete_data() {
    PlanView v;
    v.kind = "delete-data";
    Slot slot(operation_);
    if (!slot.held()) return v;
    if (leave_requested_.load()) return v;
    auto handle = reader();
    if (!removal_plan_ || !handle) {
        v.findings.push_back(make_error("E-INS-25", "plan", "there is no removal whose data folder could be deleted"));
        return v;
    }
    v.app_id = removal_plan_->app_id;
    v.app_name = removal_plan_->app_name;
    auto planned = parts_.engine.plan_delete_data(*handle, *removal_plan_);
    if (!planned.ok()) {
        v.findings = planned.findings;
        return v;
    }
    delete_plan_ = std::make_shared<install::PlanHandle>(*planned.value);
    v.ok = true;
    v.text = delete_plan_->text;
    v.summary = plain_plan_summary(delete_plan_->plan);
    if (delete_plan_->plan.data) {
        v.data_folder = delete_plan_->plan.data->path;
        v.data_files = delete_plan_->plan.data->files;
        v.data_bytes = delete_plan_->plan.data->bytes;
    }
    return v;
}

ExecView StoreService::execute_delete_data(install::CancelToken& cancel, const install::ProgressFn& progress) {
    ExecView e;
    Slot slot(operation_);
    if (!slot.held()) return e;
    if (leave_requested_.load()) return e;
    ActiveCancel active(active_cancel_, leave_requested_, cancel);
    auto handle = reader();
    if (!delete_plan_ || !handle || !delete_plan_->plan.data) {
        e.findings.push_back(make_error("E-INS-25", "plan", "there is no plan to delete the data folder; make a new plan"));
        return e;
    }
    // the user confirmed twice in the front end, each time naming the folder, the number of files and the size; the engine wants both tokens
    const std::string folder = delete_plan_->plan.data->path;
    const std::string count = std::to_string(delete_plan_->plan.data->files);
    install::DeleteResult res = parts_.engine.execute_delete_data(*handle, *delete_plan_, folder, count, progress, cancel);
    e.ok = res.done;
    e.cancelled = res.cancelled;
    e.findings = res.findings;
    if (e.ok) {
        e.text = "The data folder " + folder + " was deleted.";
        removal_plan_.reset();
    }
    delete_plan_.reset();
    e.view = list_cached();
    return e;
}

ExecView StoreService::repair(bool accept_unknown_battery, install::CancelToken& cancel, const install::ProgressFn& progress) {
    ExecView e;
    Slot slot(operation_);
    if (!slot.held()) return e;
    if (leave_requested_.load()) return e;
    ActiveCancel active(active_cancel_, leave_requested_, cancel);
    auto handle = reader();
    if (!handle || !repair_pending_) {
        e.findings.push_back(make_error("E-INS-25", "reader", "there is nothing to repair"));
        return e;
    }
    // a repair finishes or undoes a half-done operation: it needs only 10 % and is never refused for more (spec FR-016)
    const PowerDecision power = decide_power(PowerOperation::Repair, parts_.battery.percent(), parts_.battery.charging());
    if (power.result == PowerResult::Refused || (power.result == PowerResult::NeedsRiskConfirmation && !accept_unknown_battery)) {
        e.findings.push_back(power_finding(power).finding);
        return e;
    }
    install::RepairResult res = parts_.engine.repair(*handle, progress, cancel);
    e.findings = res.findings;
    e.effect = res.effect;
    e.ok = res.done;
    if (res.done) {
        repair_pending_ = false;
        WordingArgs args;
        args.app = repair_status_.app_id;
        args.repair_kind = repair_status_.kind;
        args.repair_finished = res.outcome.compare(0, 8, "finished") == 0;
        e.text = args.app.empty() ? std::string("An interrupted operation was cleaned up; nothing of any app was changed.")   // the journal never got as far as an operation
                                  : result_text(config_.facts, WordingOp::Repair, args);
        repair_status_ = RepairStatus();
    }
    for (const Finding& f : res.findings)
        if (f.rule_id == "E-INS-24" && !repair_status_.app_id.empty()) needs_repair_.insert(repair_status_.app_id);   // marked "needs repair"
    e.view = list_cached();
    return e;
}

std::vector<std::string> StoreService::update_all_queue() const {
    std::vector<std::string> ids;
    bool store = false;
    for (const AppRow& r : last_view_.rows) {
        if (r.state != RowState::UpdateAvailable) continue;
        if (r.is_store) store = true;
        else ids.push_back(r.id);
    }
    if (store) ids.push_back(kStoreAppId);   // PB Store itself comes last (spec FR-021)
    return ids;
}

}  // namespace store
}  // namespace pbstore
