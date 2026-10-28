#include "pbstore/store/store_service.h"

#include <chrono>
#include <thread>

#include "pbstore/catalog.h"
#include "pbstore/install/text_safety.h"
#include "pbstore/semver.h"
#include "pbstore/store/clock_check.h"
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
    v.rows = build_rows(listing.apps, ctx);
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

StoreOutcome StoreService::refresh(install::CancelToken& cancel, const install::ProgressFn& progress) {
    StoreOutcome out;
    parts_.last_fetch->clear();
    active_cancel_ = &cancel;
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
    active_cancel_ = nullptr;
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

void StoreService::request_leave(LeaveReason) {
    leave_requested_ = true;
    if (install::CancelToken* c = active_cancel_.load()) c->cancel();   // a download stops and writes nothing; Wi-Fi is restored by the session
}

}  // namespace store
}  // namespace pbstore
