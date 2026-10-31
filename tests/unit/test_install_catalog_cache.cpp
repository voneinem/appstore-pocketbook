// T013 (engine addition E1): the engine can list from a catalog that was accepted earlier. The cached bytes are
// verified again with the same function a refresh uses, nothing is requested, and nothing is written.
#include "install_rig.h"
#include "pbstore/install/catalog_cache.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}

// An in-memory cache that counts its calls.
class FakeCache : public CatalogCache {
public:
    bool load(std::string* catalog, std::string* signature) override {
        ++loads;
        if (!present) return false;
        *catalog = catalog_bytes;
        *signature = signature_bytes;
        return true;
    }
    bool store(const std::string& catalog, const std::string& signature) override {
        ++stores;
        catalog_bytes = catalog;
        signature_bytes = signature;
        present = true;
        return true;
    }
    void discard() override {
        ++discards;
        present = false;
        catalog_bytes.clear();
        signature_bytes.clear();
    }
    bool present = false;
    std::string catalog_bytes, signature_bytes;
    int loads = 0, stores = 0, discards = 0;
};

// A downloader that must never be called.
struct NeverCalled {
    std::shared_ptr<int> calls = std::make_shared<int>(0);
    Downloader downloader() const {
        auto c = calls;
        return [c](const std::string&, uint64_t, long, const DownloadProgress&) {
            ++*c;
            FetchResult r;
            r.ok = false;
            r.kind = FetchKind::Unreachable;
            r.error = "no network in this test";
            return r;
        };
    }
};

struct Rig {
    EngineRig rig{with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0"))};
    FakeCache cache;
    bool ready() const { return rig.ready(); }
    void prepare() {
        CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
        e.listed_versions = {"1.2.0", "1.3.0"};
        rig.host.add_app(nested_sample_app("other", "1.0.0", "U_Other"));
        rig.host.publish(100);
        rig.write_reader();
    }
};

bool same_states(const std::vector<AppEntry>& a, const std::vector<AppEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || a[i].state != b[i].state || a[i].installed_version != b[i].installed_version ||
            a[i].offered_version != b[i].offered_version || a[i].update_available != b[i].update_available || a[i].offered != b[i].offered ||
            a[i].not_in_catalog != b[i].not_in_catalog || a[i].description != b[i].description)
            return false;
    }
    return true;
}

}  // namespace

PB_TEST(the_cached_list_equals_the_online_list_for_the_same_reader) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    Engine& online = r.rig.make_engine(nullptr, {}, &r.cache);
    auto handle = online.open_reader(r.rig.root());
    PB_CHECK(handle.ok());
    CancelToken cancel;
    auto listing = online.list(**handle.value, nullptr, cancel);
    PB_CHECK(listing.ok());
    PB_CHECK(!listing.value->apps.empty());

    NeverCalled never;
    Engine& offline = r.rig.make_engine(never.downloader(), {}, &r.cache);
    auto handle2 = offline.open_reader(r.rig.root());
    PB_CHECK(handle2.ok());
    auto cached = offline.list_cached(**handle2.value);
    PB_CHECK(cached.ok());
    PB_CHECK(cached.value->catalog_loaded);
    PB_CHECK_EQ(cached.value->catalog_generated_at, listing.value->catalog_generated_at);
    PB_CHECK(same_states(cached.value->apps, listing.value->apps));
    PB_CHECK_EQ(*never.calls, 0);                        // list_cached never touches the network
}

PB_TEST(a_flipped_byte_in_the_catalog_or_the_signature_is_refused_with_e_cat_01_and_the_cache_is_discarded) {
    for (bool flip_signature : {false, true}) {
        Rig r;
        if (!r.ready()) PB_SKIP("openssl CLI not available");
        r.prepare();
        Engine& online = r.rig.make_engine(nullptr, {}, &r.cache);
        auto handle = online.open_reader(r.rig.root());
        CancelToken cancel;
        PB_CHECK(online.list(**handle.value, nullptr, cancel).ok());
        PB_CHECK(r.cache.present);
        std::string& target = flip_signature ? r.cache.signature_bytes : r.cache.catalog_bytes;
        target[target.size() / 2] = static_cast<char>(target[target.size() / 2] ^ 0x01);

        NeverCalled never;
        Engine& offline = r.rig.make_engine(never.downloader(), {}, &r.cache);
        auto handle2 = offline.open_reader(r.rig.root());
        auto cached = offline.list_cached(**handle2.value);
        PB_CHECK(!cached.ok());
        PB_CHECK(has_rule(cached.findings, "E-CAT-01"));
        PB_CHECK_EQ(r.cache.discards, 1);
        PB_CHECK(!r.cache.present);
        PB_CHECK_EQ(*never.calls, 0);
    }
}

PB_TEST(without_a_cache_there_is_no_listing_and_a_notice_says_so) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    NeverCalled never;
    Engine& engine = r.rig.make_engine(never.downloader());          // Deps::catalog_cache == nullptr
    auto handle = engine.open_reader(r.rig.root());
    auto cached = engine.list_cached(**handle.value);
    PB_CHECK(cached.ok());
    PB_CHECK(!cached.value->catalog_loaded);
    bool found = false;                                                 // only what is on the reader: the installed testapp
    for (const auto& a : cached.value->apps) {
        if (a.id == "testapp") { found = true; PB_CHECK(a.state == AppStateKind::Installed); PB_CHECK(a.offered_version.empty()); }
        PB_CHECK(a.id != "other");
    }
    PB_CHECK(found);
    bool noticed = false;
    for (const auto& n : cached.notices) if (n.message.find("no cached catalog") != std::string::npos) noticed = true;
    PB_CHECK(noticed);
    PB_CHECK_EQ(*never.calls, 0);

    FakeCache empty;                                                  // a cache that holds nothing yet
    Engine& with_empty = r.rig.make_engine(never.downloader(), {}, &empty);
    auto handle2 = with_empty.open_reader(r.rig.root());
    auto cached2 = with_empty.list_cached(**handle2.value);
    PB_CHECK(cached2.ok());
    PB_CHECK(!cached2.value->catalog_loaded);
    PB_CHECK_EQ(empty.discards, 0);                                   // nothing to discard
}

PB_TEST(store_is_called_exactly_once_after_an_accepted_refresh_and_never_after_a_rejected_one) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    Engine& engine = r.rig.make_engine(nullptr, {}, &r.cache);
    auto handle = engine.open_reader(r.rig.root());
    CancelToken cancel;
    PB_CHECK(engine.list(**handle.value, nullptr, cancel).ok());
    PB_CHECK_EQ(r.cache.stores, 1);
    PB_CHECK_EQ(r.cache.catalog_bytes, r.rig.host.files["/catalog.json"]);
    PB_CHECK_EQ(r.cache.signature_bytes, r.rig.host.files["/catalog.json.sig"]);
    const std::string accepted_bytes = r.cache.catalog_bytes;

    r.rig.host.publish(90);                                           // older than the remembered 100: rollback
    auto rolled_back = engine.list(**handle.value, nullptr, cancel);
    PB_CHECK(!rolled_back.ok());
    PB_CHECK(has_rule(rolled_back.findings, "E-CAT-04"));
    PB_CHECK_EQ(r.cache.stores, 1);                                   // still one: a rejected catalog is never stored
    PB_CHECK_EQ(r.cache.catalog_bytes, accepted_bytes);               // the cache still holds the accepted catalog

    r.rig.host.publish(101);
    r.rig.host.files["/catalog.json"] += " ";                         // the signature no longer matches
    auto tampered = engine.list(**handle.value, nullptr, cancel);
    PB_CHECK(!tampered.ok());
    PB_CHECK(has_rule(tampered.findings, "E-CAT-01"));
    PB_CHECK_EQ(r.cache.stores, 1);
}

PB_TEST(list_cached_makes_no_serial_comparison_and_writes_nothing_to_the_reader) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI not available");
    r.prepare();
    Engine& online = r.rig.make_engine(nullptr, {}, &r.cache);
    auto handle = online.open_reader(r.rig.root());
    CancelToken cancel;
    PB_CHECK(online.list(**handle.value, nullptr, cancel).ok());      // remembers serial 100 and caches that catalog
    // the remembered serial is now higher than the cache (for example a hand-copied older cache): it is still shown
    StateStore(r.rig.state).record(500);
    Snapshot before = snapshot(r.rig.root());
    NeverCalled never;
    Engine& offline = r.rig.make_engine(never.downloader(), {}, &r.cache);
    auto handle2 = offline.open_reader(r.rig.root());
    auto cached = offline.list_cached(**handle2.value);
    PB_CHECK(cached.ok());
    PB_CHECK(cached.value->catalog_loaded);
    PB_CHECK(compare(before, snapshot(r.rig.root())).identical);
    PB_CHECK_EQ(StateStore(r.rig.state).last_serial().value_or(0), int64_t(500));
}

PB_TEST_MAIN()
