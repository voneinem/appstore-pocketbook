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
#include "pbstore/store/catalog_cache.h"
#include "pbstore/store/reader_storage.h"

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
    pbstore::install::Redactor redactor;
    std::unique_ptr<pbstore::install::Log> log;
    std::unique_ptr<pbstore::store::StoreCatalogCache> cache;
    std::unique_ptr<pbstore::install::Engine> engine;

    explicit StoreRig(const FakeReader& r = FakeReader()) : host(key), reader(r), storage(pbstore::store::make_reader_storage(dir.path() + "/mnt/ext1")) {
        log = std::make_unique<pbstore::install::Log>(*storage.state, clock, redactor, "pbstore.log");
        cache = std::make_unique<pbstore::store::StoreCatalogCache>(*storage.state, clock);
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
        deps.download = download ? download : host.downloader("");
        deps.clock = &clock;
        deps.log = log.get();
        deps.redactor = &redactor;
        deps.identity_sources = sources;
        deps.catalog_cache = cache.get();
        engine = std::make_unique<pbstore::install::Engine>(config, deps);
        return *engine;
    }
};

}  // namespace pbtest
