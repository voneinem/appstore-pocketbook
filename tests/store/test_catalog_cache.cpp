// T042: the store's catalog cache over the state folder (research R-09): the exact bytes of the last accepted catalog and its
// signature, the date of the refresh, and the serial memory that is the larger of state.json and the cache.
// The write order is state.json first (the engine does it), then both temporary files, then the signature, then the
// catalog: a cut after any call leaves a cache that verifies or is discarded, and never a serial below the cache's.
#include "cut_state_dirs.h"
#include "fixed_clock.h"
#include "install_rig.h"
#include "mem_state_dirs.h"
#include "pbstore/catalog_verify.h"
#include "pbstore/install/state_store.h"
#include "pbstore/store/catalog_cache.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

struct Fixture {
    KeyPair key = make_keypair();
    CatalogHost host{key};
    FixedClock clock;
    Fixture() {
        if (host.ready()) {
            host.add_app(nested_sample_app());
            host.publish(100);
        }
    }
    std::vector<std::string> keys() const { return {key.pub}; }
    std::string catalog_at(int64_t serial) {
        host.publish(serial);
        return host.files["/catalog.json"];
    }
    std::string signature() { return host.files["/catalog.json.sig"]; }
};

bool verifies(StoreCatalogCache& cache, const std::vector<std::string>& keys, int64_t* serial) {   // the engine's check, as list_cached does it
    std::string c, s;
    if (!cache.load(&c, &s)) return false;
    VerifyResult v = verify_catalog(c, s, keys, 0);
    if (v.ok() && serial) *serial = v.serial;
    return v.ok();
}

}  // namespace

PB_TEST(a_stored_catalog_is_loaded_back_byte_for_byte_with_the_refresh_date) {
    Fixture f;
    if (!f.host.ready()) PB_SKIP("openssl CLI not available");
    MemStateDirs dirs;
    StoreCatalogCache cache(dirs, f.clock);
    std::string c, s;
    PB_CHECK(!cache.load(&c, &s));                                    // nothing yet
    PB_CHECK(!cache.refreshed_at().has_value());
    const std::string bytes = f.catalog_at(100);
    PB_CHECK(cache.store(bytes, f.signature()));
    PB_CHECK(cache.load(&c, &s));
    PB_CHECK_EQ(c, bytes);
    PB_CHECK_EQ(s, f.signature());
    PB_CHECK_EQ(dirs.text("catalog.meta"), std::string("refreshed_at=2026-10-25T12:00:00Z\n"));
    PB_CHECK(cache.refreshed_at() == std::optional<std::string>("2026-10-25T12:00:00Z"));
    PB_CHECK(!dirs.exists("catalog.json.tmp"));
    PB_CHECK(!dirs.exists("catalog.json.sig.tmp"));
}

PB_TEST(discard_removes_only_the_cache_files) {
    Fixture f;
    if (!f.host.ready()) PB_SKIP("openssl CLI not available");
    MemStateDirs dirs;
    StoreCatalogCache cache(dirs, f.clock);
    PB_CHECK(cache.store(f.catalog_at(100), f.signature()));
    StateStore(dirs).record(100);
    dirs.write_atomic("pbstore.log", "log\n");
    dirs.write_atomic("pbstore.log.1", "old\n");
    dirs.write_atomic("unknown.txt", "mine\n");
    dirs.write_atomic("catalog.json.tmp", "partial");
    cache.discard();
    PB_CHECK(!dirs.exists("catalog.json"));
    PB_CHECK(!dirs.exists("catalog.json.sig"));
    PB_CHECK(!dirs.exists("catalog.meta"));
    PB_CHECK(!dirs.exists("catalog.json.tmp"));
    PB_CHECK(dirs.exists("state.json"));                              // the serial memory stays
    PB_CHECK(dirs.exists("pbstore.log"));
    PB_CHECK(dirs.exists("pbstore.log.1"));
    PB_CHECK(dirs.exists("unknown.txt"));                             // anything the store did not create is not its to delete
    cache.discard();                                                  // again: harmless
}

PB_TEST(a_stored_catalog_and_signature_over_their_limits_are_not_loaded) {
    MemStateDirs dirs;
    FixedClock clock;
    StoreCatalogCache cache(dirs, clock);
    dirs.write_atomic("catalog.json", std::string(1024 * 1024 + 1, 'x'));
    dirs.write_atomic("catalog.json.sig", "sig");
    std::string c, s;
    PB_CHECK(!cache.load(&c, &s));
    dirs.write_atomic("catalog.json", "{}");
    dirs.write_atomic("catalog.json.sig", std::string(1025, 'x'));
    PB_CHECK(!cache.load(&c, &s));
    dirs.write_atomic("catalog.json.sig", "sig");
    PB_CHECK(cache.load(&c, &s));
    dirs.remove("catalog.json.sig");
    PB_CHECK(!cache.load(&c, &s));                                    // a catalog without its signature is not loaded
}

PB_TEST(the_refresh_date_is_never_when_the_file_is_missing_oversized_or_malformed) {
    MemStateDirs dirs;
    FixedClock clock;
    StoreCatalogCache cache(dirs, clock);
    PB_CHECK(!cache.refreshed_at().has_value());
    for (const std::string& bad : {std::string("refreshed_at=yesterday\n"), std::string("nonsense"), std::string(200, 'x'), std::string("refreshed_at=\n")}) {
        dirs.write_atomic("catalog.meta", bad);
        PB_CHECK(!cache.refreshed_at().has_value());
    }
    dirs.write_atomic("catalog.meta", "refreshed_at=2026-01-02T03:04:05Z\n");
    PB_CHECK(cache.refreshed_at() == std::optional<std::string>("2026-01-02T03:04:05Z"));
}

PB_TEST(a_cut_after_any_call_of_an_accepted_refresh_leaves_a_cache_that_verifies_or_is_discarded_and_never_a_lower_serial) {
    Fixture f;
    if (!f.host.ready()) PB_SKIP("openssl CLI not available");
    const std::string old_bytes = f.catalog_at(100), old_sig = f.signature();
    const std::string new_bytes = f.catalog_at(101), new_sig = f.signature();
    int total = 0;
    for (int cut = 1; cut <= 40; ++cut) {
        MemStateDirs mem;
        CuttingStateDirs dirs(mem);
        StoreCatalogCache cache(dirs, f.clock);
        StateStore state(dirs);
        PB_CHECK(state.record(100));                                  // the earlier refresh
        PB_CHECK(cache.store(old_bytes, old_sig));
        dirs.cut_at(cut);                                             // power is lost at the cut-th call of the next refresh
        state.record(101);                                            // what the engine does first (refresh_catalog)
        cache.store(new_bytes, new_sig);
        const bool whole = !dirs.lost();
        total = dirs.mutating_calls();
        // after the restart
        CuttingStateDirs after(mem);
        StoreCatalogCache reborn(after, f.clock);
        int64_t cached_serial = 0;
        const bool ok = verifies(reborn, f.keys(), &cached_serial);
        const int64_t remembered = StateStore(after).last_serial().value_or(0);
        const int64_t effective = reborn.effective_last_serial(f.keys()).value_or(0);
        // a cache that does not verify (ok == false) is discarded by the engine when it is loaded
        if (ok) {
            PB_CHECK(cached_serial == 100 || cached_serial == 101);   // the old one or the new one, complete
            PB_CHECK(effective >= cached_serial);                     // never a serial below the cache's
        }
        PB_CHECK(effective >= remembered);
        if (whole) PB_CHECK_EQ(effective, int64_t(101));
        PB_CHECK(remembered >= 100);                                  // never lower than what was accepted before
        if (whole) break;
    }
    PB_CHECK(total >= 4);                                             // the refresh really made several writes to cut between
}

PB_TEST(the_serial_memory_is_the_larger_of_state_json_and_the_verified_cache) {
    Fixture f;
    if (!f.host.ready()) PB_SKIP("openssl CLI not available");
    MemStateDirs dirs;
    StoreCatalogCache cache(dirs, f.clock);
    PB_CHECK(!cache.effective_last_serial(f.keys()).has_value());     // both missing: the first-run rule
    StateStore(dirs).record(50);
    PB_CHECK_EQ(cache.effective_last_serial(f.keys()).value_or(0), int64_t(50));
    PB_CHECK(cache.store(f.catalog_at(100), f.signature()));
    PB_CHECK_EQ(cache.effective_last_serial(f.keys()).value_or(0), int64_t(100));   // the cache is higher
    StateStore(dirs).record(200);
    PB_CHECK_EQ(cache.effective_last_serial(f.keys()).value_or(0), int64_t(200));   // state.json is higher
    dirs.remove("state.json");
    PB_CHECK_EQ(cache.effective_last_serial(f.keys()).value_or(0), int64_t(100));   // a missing state.json does not reset protection
    dirs.write_atomic("state.json", "damaged");
    PB_CHECK_EQ(cache.effective_last_serial(f.keys()).value_or(0), int64_t(100));
    // a cache that does not verify counts for nothing
    dirs.write_atomic("catalog.json", f.catalog_at(300) + " ");
    PB_CHECK(!cache.effective_last_serial(f.keys()).has_value());
}

PB_TEST(restoring_the_serial_memory_raises_a_missing_state_json_to_the_cached_serial) {
    Fixture f;
    if (!f.host.ready()) PB_SKIP("openssl CLI not available");
    MemStateDirs dirs;
    StoreCatalogCache cache(dirs, f.clock);
    PB_CHECK(cache.store(f.catalog_at(100), f.signature()));
    PB_CHECK(!StateStore(dirs).last_serial().has_value());
    cache.restore_serial_memory(f.keys());
    PB_CHECK_EQ(StateStore(dirs).last_serial().value_or(0), int64_t(100));
    StateStore(dirs).record(150);
    cache.restore_serial_memory(f.keys());                            // never lowers
    PB_CHECK_EQ(StateStore(dirs).last_serial().value_or(0), int64_t(150));
}

PB_TEST_MAIN()
