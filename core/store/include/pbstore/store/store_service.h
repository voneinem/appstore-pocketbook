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
    std::optional<int> battery_percent;
    std::optional<uint64_t> free_bytes;
};

struct StartResult {
    std::vector<Finding> notices;             // W-STO-01 (cancelled, nothing changed), E-STO-06 and E-STO-07 (what the launcher left)
    bool repair_pending = false;              // an interrupted operation was found: the repair prompt comes first
    std::optional<install::RepairPlan> repair;
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
    // The first screen is shown after the start check: the start counts as successful (spec FR-034). Removes the launcher's marker and
    // remembers the version that reached its first screen (the next start can then say "PB Store was updated to X").
    void mark_started(const std::string& version);
    // The user left (home key, sleep, cable, the reader ended the app): stops what runs; a download stops and writes nothing.
    void request_leave(LeaveReason reason);
    bool leave_requested() const { return leave_requested_.load(); }

private:
    std::shared_ptr<install::ReaderHandle> reader();
    ListView view_from(const install::Listing& listing, std::vector<Finding> notices);
    std::vector<Finding> adapt(std::vector<Finding> findings);
    std::vector<std::string> trusted_keys() const { return parts_.engine.config().trusted_keys; }

    StoreParts parts_;
    StoreConfig config_;
    std::shared_ptr<install::ReaderHandle> reader_;
    ListView last_view_;
    RunMessage launcher_message_;
    std::atomic<bool> leave_requested_{false};
    std::atomic<install::CancelToken*> active_cancel_{nullptr};
};

}  // namespace store
}  // namespace pbstore
