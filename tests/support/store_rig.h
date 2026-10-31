#pragma once
// Test helper for the store app (feature 003): a fake reader tree at <tmp>/mnt/ext1 opened through the reader's configuration
// (make_reader_storage), a catalog host with a throw-away CA that serves a signed catalog and packages, the fakes of the device
// interfaces (Wi-Fi, battery 80 %, memory 512 MiB, screen), a fixed clock, the engine wired as the store wires it (program name
// "pbstore-app", the store's catalog cache, the store's log pbstore.log), request capture, and a count of connection attempts.
// Nothing here talks to the internet. The rig builds a StoreService once store_service.h exists (task T072).

#include <memory>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "fake_battery.h"
#include "fake_memory.h"
#include "fake_radio.h"
#include "fake_screen.h"
#include "fixed_clock.h"
#include "install_rig.h"
#include "pbstore/install/engine.h"
#include "pbstore/install/posix_state_dirs.h"
#include "pbstore/store/catalog_cache.h"
#include "pbstore/store/last_fetch.h"
#include "pbstore/store/pending_note.h"
#include "pbstore/store/reader_storage.h"
#include "pbstore/store/run_state.h"
#include "pbstore/store/store_redact.h"
#include "pbstore/store/store_service.h"

namespace pbtest {

struct StoreRig {
    TmpDir dir;
    KeyPair key = make_keypair();
    CatalogHost host;
    FakeReader reader;
    pbstore::store::ReaderStorage storage;
    FakeRadio radio;
    FakeBattery battery;
    FakeMemory memory;
    FakeScreen screen;
    FixedClock clock;
    pbstore::store::StoreRedactor redactor;
    std::unique_ptr<pbstore::install::Log> log;
    std::unique_ptr<pbstore::store::StoreCatalogCache> cache;
    std::unique_ptr<pbstore::install::PosixStateDirs> run_dirs;   // system/pbstore/app/run: the launcher's files
    std::unique_ptr<pbstore::store::RunState> run_state;
    std::unique_ptr<pbstore::store::PendingNote> pending_note;
    std::shared_ptr<pbstore::store::LastFetch> last_fetch = std::make_shared<pbstore::store::LastFetch>();
    std::unique_ptr<pbstore::install::Engine> engine;
    int64_t virtual_ms = 0;                                       // the service's clock: sleeping moves it, nothing waits

    explicit StoreRig(const FakeReader& r = FakeReader()) : host(key), reader(r), storage(pbstore::store::make_reader_storage(dir.path() + "/mnt/ext1")) {
        log = std::make_unique<pbstore::install::Log>(*storage.state, clock, redactor, "pbstore.log");
        cache = std::make_unique<pbstore::store::StoreCatalogCache>(*storage.state, clock);
        run_dirs = std::make_unique<pbstore::install::PosixStateDirs>(dir.path() + "/mnt/ext1/system/pbstore/app/run");
        run_state = std::make_unique<pbstore::store::RunState>(*storage.state, *run_dirs);
        pending_note = std::make_unique<pbstore::store::PendingNote>(*storage.state);
    }

    bool ready() const { return host.ready(); }
    std::string root() const { return dir.path() + "/mnt/ext1"; }
    // Writes the fake reader (applications, desktop configuration, any installed apps) to <tmp>/mnt/ext1.
    void write_reader() {
        ::mkdir((dir.path() + "/mnt").c_str(), 0755);
        reader.write_to(root());
    }
    int connection_attempts() const { return host.connection_attempts(); }
    std::vector<std::string> requests() const { return host.raw_requests(); }

    // The engine as the store wires it. `download` defaults to the catalog host's downloader with no User-Agent (spec FR-043).
    pbstore::install::Engine& make_engine(const pbstore::install::Downloader& download = nullptr, std::vector<pbstore::install::IdentitySource*> sources = {}) {
        pbstore::install::Config config;
        config.catalog_url = host.catalog_url();
        config.trusted_keys = {key.pub};
        config.program_version = "0.1.0";
        config.program_name = "pbstore-app";
        pbstore::install::Deps deps;
        deps.open_fs = storage.open_fs;
        deps.mounts = storage.mounts.get();
        deps.state = storage.state.get();
        deps.download = pbstore::store::observe_fetches(download ? download : host.downloader(""), last_fetch, &redactor);
        deps.clock = &clock;
        deps.log = log.get();
        deps.redactor = &redactor;
        deps.identity_sources = sources;
        deps.catalog_cache = cache.get();
        engine = std::make_unique<pbstore::install::Engine>(config, deps);
        return *engine;
    }

    // The store's service over this rig. The release date is before the fixed clock, so a certificate failure does not look like a
    // wrong clock unless a test moves the clock back.
    std::unique_ptr<pbstore::store::StoreService> make_service(pbstore::store::StoreConfig config = {}) {
        if (config.release_date.empty()) config.release_date = "2026-10-01";
        if (config.running_version.empty()) config.running_version = "0.1.0";
        if (config.catalog_url.empty()) config.catalog_url = host.catalog_url();
        if (!config.sleep_ms) config.sleep_ms = [this](int ms) { virtual_ms += ms; };
        if (!config.now_ms) config.now_ms = [this] { return virtual_ms; };
        pbstore::store::StoreParts parts{*engine, storage, radio, battery, memory, screen, *cache, *run_state, *pending_note, clock, last_fetch};
        return std::make_unique<pbstore::store::StoreService>(parts, config);
    }
};

}  // namespace pbtest
