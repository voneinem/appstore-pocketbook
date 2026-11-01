#pragma once
// The Qt-free façade the bridge calls (research R-16). It composes the engine of feature 002 with what only the reader needs: the
// catalog cache and the serial memory, the Wi-Fi session, the battery and memory gates, the launcher's state, the cancel note. It
// contains no install logic (spec FR-014): planning, staging, the journal and recovery stay in the engine. This first slice carries the
// browsing story: the list from the cache (no network, no radio), Refresh, the start check, and the facts for Settings. The write
// actions (install, update, remove, adopt, repair) are added by the later stories on the same class.
//
// Nothing here makes a network call or touches Wi-Fi except refresh() and the actions of the later stories, and only after the user
// asked (spec FR-001, SC-004).

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pbstore/install/engine.h"
#include "pbstore/install/messages.h"
#include "pbstore/rules.h"
#include "pbstore/store/app_row.h"
#include "pbstore/store/battery.h"
#include "pbstore/store/catalog_cache.h"
#include "pbstore/store/device_facts.h"
#include "pbstore/store/last_fetch.h"
#include "pbstore/store/memory_info.h"
#include "pbstore/store/pending_note.h"
#include "pbstore/store/radio.h"
#include "pbstore/store/radio_session.h"
#include "pbstore/store/reader_storage.h"
#include "pbstore/store/run_state.h"
#include "pbstore/store/screen.h"

namespace pbstore {
namespace store {

// Why the user left: all of them are handled the same way (spec FR-025). Which events the reader really sends is a device fact.
enum class LeaveReason { Home, Sleep, Usb, ReaderEnded };

struct StoreConfig {
    std::string release_date;                 // YYYY-MM-DD from app/RELEASE_DATE: the floor of the clock check
    std::string running_version;              // compiled in from app/VERSION
    std::string catalog_url;                  // shown in Settings; the engine's Config holds the address it uses
    std::string trust_source = "unknown";     // "the device's own certificate store" or "the bundled certificate file"
    DeviceFacts facts;                        // restart wording
    std::function<std::string()> reader_identity;   // what the reader says about itself, shown in Settings (empty: "unknown")
    RadioSession::Sleep sleep_ms;             // defaults to really sleeping
    RadioSession::Now now_ms;                 // defaults to a monotonic clock
};

// The objects the service works with. The service owns none of them.
struct StoreParts {
    install::Engine& engine;
    ReaderStorage& storage;
    Radio& radio;
    Battery& battery;
    MemoryInfo& memory;
    Screen& screen;
    StoreCatalogCache& cache;
    RunState& run_state;
    PendingNote& pending_note;
    install::Clock& clock;
    std::shared_ptr<LastFetch> last_fetch;
};

struct CatalogInfo {
    bool loaded = false;                      // a verified catalog is the source of the list
    bool discarded = false;                   // the cached catalog failed its check and was thrown away (E-STO-09)
    std::string refreshed_at;                 // the reader's clock at the last successful refresh, "" if never
    std::string generated_at;                 // the catalog's own date
};

struct ListView {
    std::vector<AppRow> rows;                 // unfiltered, in the order of the list
    CatalogInfo catalog;
    std::vector<Finding> notices;
};

// What an action returns to the front end: whether it worked, why not, and the list as it is afterwards (after a failure the list
// still comes from the cache).
struct StoreOutcome {
    bool ok = false;
    std::vector<Finding> findings;            // errors, already in the store's words (E-STO-04 instead of E-INS-26 on a wrong clock)
    std::vector<Finding> notices;             // W-INS-03, W-STO-02, W-STO-03 ...
    ListView view;
    std::vector<install::Message> messages() const;           // the findings as three-part messages
    std::vector<install::Message> notice_messages() const;
};

struct SettingsFacts {
    std::string catalog_url;
    std::string refreshed_at, generated_at;   // "" if never
    std::string trust_source;
    std::string custom_ca = "not supported";  // there is no custom CA file (FR-004)
    std::string log_location;
    std::string reader_identity = "unknown";  // "model 1030, firmware 6.11.1437", or the text that could not be read
    std::optional<int> battery_percent;
    std::optional<uint64_t> free_bytes;
};

struct StartResult {
    std::vector<Finding> notices;             // W-STO-01 (cancelled, nothing changed), E-STO-06 and E-STO-07 (what the launcher left)
    bool repair_pending = false;              // an interrupted operation was found: the repair prompt comes first
    std::optional<install::RepairPlan> repair;
};

// One consent the plan names, shown as its own tap control (never preselected, never "yes to all"; spec FR-015).
struct ConfirmationItem {
    std::string name;    // the engine's stable name: "edited-files", "unknown-firmware", ...
    std::string label;   // the words next to the control
};

// A plan waiting for the user's confirmation (stories 2 to 5). `text` is the engine's plan text, unchanged (spec FR-014); `summary` is what an end user reads first.
struct PlanView {
    bool ok = false;
    std::vector<Finding> findings;
    std::vector<Finding> notices;
    std::string kind;                         // install, update, remove, adopt
    std::string app_id, app_name;
    std::string text;
    std::string summary;                      // the plain sentences the page shows first; `text` is behind "Show details"
    std::vector<ConfirmationItem> confirmations;   // beside the plan itself, which is the Confirm button
    bool nothing_to_do = false;               // W-INS-01: already at the offered version
    bool battery_unreadable = false;          // the level cannot be read: the user must confirm the risk (E-STO-05)
    bool is_store = false;                    // an update of PB Store itself
    std::string extra;                        // a line below the plan text (the self-update note)
    std::string data_folder;                  // remove: the app's data folder, "" if none
    uint64_t data_files = 0, data_bytes = 0;
    std::vector<install::Message> messages() const;
};

// One app of an "Update all" run, as the combined confirmation lists it (feature 004).
struct UpdateAllEntry {
    std::string id, name, installed_version, offered_version;
    int64_t package_size = 0;                 // from the signed catalog entry
    bool is_store = false;
};

// What the combined confirmation of "Update all" shows (feature 004, spec FR-009). Built from the cached catalog and the reader's contents:
// no network call, no write, no Wi-Fi.
struct UpdateAllPreview {
    bool ok = false;                          // false while an interrupted operation waits for its repair (E-INS-23)
    std::vector<Finding> findings;
    std::vector<UpdateAllEntry> entries;      // in run order, PB Store last; empty exactly when update_all_queue() is empty
    int64_t total_bytes = 0;                  // the sum of the package sizes
    bool includes_store = false;
    // consents about the reader itself, asked once for the whole run instead of at every app: "unknown-model", "unknown-firmware"
    std::vector<ConfirmationItem> reader_consents;
    bool battery_unreadable = false;          // the level cannot be read: one tap states the risk for the run (E-STO-05)
};

// What an executed plan returns: the result text on success, the three-part messages otherwise, and the list as it is afterwards.
struct ExecView {
    bool ok = false;
    bool cancelled = false;
    install::ReaderEffect effect = install::ReaderEffect::Unchanged;
    std::vector<Finding> findings;
    std::vector<Finding> notices;
    std::string text;
    bool data_folder_left = false;            // after a removal: the data folder stayed and could be deleted on request
    ListView view;
    std::vector<install::Message> messages() const;
};

// An interrupted operation found at start (story 6).
struct RepairStatus {
    bool pending = false;
    std::string text;                         // the engine's repair plan as shown
    std::string kind, app_id, from_version, to_version;
    std::string action;                       // "finish" or "undo"
    std::string reason;
};

class StoreService {
public:
    StoreService(StoreParts parts, StoreConfig config);

    // At start, before the list and before any network use: the cancel note, the launcher's message, an interrupted operation.
    StartResult start_check();
    // The list from the cache and the reader's current contents. Never uses the network or the radio.
    ListView list_cached();
    // The user tapped Refresh: Wi-Fi on if needed, the catalog and its signature, the consumer rules, Wi-Fi off again.
    StoreOutcome refresh(install::CancelToken& cancel, const install::ProgressFn& progress);
    // The rows of the last list, narrowed by the search text.
    std::vector<AppRow> rows(const std::string& filter) const;
    SettingsFacts settings_facts();

    // ---- the write actions (all through the engine; the store adds the gates, the Wi-Fi session and the note) ----
    // Builds the plan: battery and memory checks, Wi-Fi on if the package must be downloaded, the engine's plan (download, verification,
    // contract rules, reader checks), Wi-Fi off again. Writes nothing to the reader's apps or desktop configuration.
    PlanView plan_action(RowAction action, const std::string& app_id, install::CancelToken& cancel, const install::ProgressFn& progress);
    void discard_plan();
    // Carries the plan out after the user confirmed it (and the consents `given` names). The battery is checked again immediately before
    // the first write. `accept_unknown_battery` is the tap that states the risk (E-STO-05).
    ExecView execute_plan(const std::vector<std::string>& given, bool accept_unknown_battery, install::CancelToken& cancel, const install::ProgressFn& progress);
    // After a removal that kept the data folder: the plan to delete it (two confirmations by the user, each naming the folder, the number of files
    // and the size), and its execution.
    PlanView plan_delete_data();
    ExecView execute_delete_data(install::CancelToken& cancel, const install::ProgressFn& progress);
    // An interrupted operation, found at start: shown first; nothing else can be started until it is repaired.
    RepairStatus repair_status() const { return repair_status_; }
    ExecView repair(bool accept_unknown_battery, install::CancelToken& cancel, const install::ProgressFn& progress);
    // The apps with an update, PB Store itself last (spec FR-021).
    std::vector<std::string> update_all_queue() const;
    // The combined confirmation of "Update all" (feature 004): the apps of update_all_queue() with both versions and sizes, the reader consents and
    // the battery. Reads only; the run itself is one plan_action and one execute_plan per app.
    UpdateAllPreview update_all_preview();

    // The first screen is shown after the start check: the start counts as successful (spec FR-034). Removes the launcher's marker and
    // remembers the version that reached its first screen (the next start can then say "PB Store was updated to X").
    void mark_started(const std::string& version);
    // The user left (home key, sleep, cable, the reader ended the app): stops what runs; a download stops and writes nothing.
    void request_leave(LeaveReason reason);
    bool leave_requested() const { return leave_requested_.load(); }
    // The reader came back (wake from sleep): operations may start again.
    void resume();

private:
    std::shared_ptr<install::ReaderHandle> reader();
    ListView view_from(const install::Listing& listing, std::vector<Finding> notices);
    std::vector<Finding> adapt(std::vector<Finding> findings);
    std::vector<std::string> trusted_keys() const { return parts_.engine.config().trusted_keys; }

    StoreParts parts_;
    StoreConfig config_;
    // the state of the one operation that can be in progress (spec FR-024)
    std::atomic<bool> operation_{false};
    std::shared_ptr<install::PlanHandle> plan_;
    std::optional<install::Plan> removal_plan_;      // the last removal, for the optional deletion of the data folder
    std::shared_ptr<install::PlanHandle> delete_plan_;
    bool repair_pending_ = false;
    RepairStatus repair_status_;
    std::set<std::string> needs_repair_;
    std::shared_ptr<install::ReaderHandle> reader_;
    ListView last_view_;
    RunMessage launcher_message_;
    std::atomic<bool> leave_requested_{false};
    std::atomic<install::CancelToken*> active_cancel_{nullptr};
};

}  // namespace store
}  // namespace pbstore
