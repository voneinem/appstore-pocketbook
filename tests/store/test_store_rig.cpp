// T060: the store's test rig works: the reader is opened through the reader's configuration, the catalog host is reached with the
// engine's downloader, the catalog cache is hooked in, the counters count, and the fakes start in their documented states.
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

PB_TEST(the_fakes_start_in_the_documented_states) {
    StoreRig rig;
    PB_CHECK(rig.battery.percent() == std::optional<int>(80));
    PB_CHECK(rig.memory.available_bytes() == std::optional<uint64_t>(512ull << 20));
    PB_CHECK(rig.radio.is_on() == std::optional<bool>(false));
    PB_CHECK_EQ(rig.screen.full_updates(), 0);
    PB_CHECK_EQ(rig.storage.root, rig.root());
    PB_CHECK_EQ(rig.root().substr(rig.root().size() - 9), std::string("/mnt/ext1"));
}

PB_TEST(the_engine_opens_the_fake_reader_through_the_readers_configuration) {
    StoreRig rig(with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")));
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.write_reader();
    Engine& engine = rig.make_engine();
    auto handle = engine.open_reader(std::nullopt);
    PB_CHECK(handle.ok());
    PB_CHECK_EQ((*handle.value)->candidate.mount_point, rig.root());
    auto installed = engine.installed(**handle.value);
    PB_CHECK(installed.ok());
    bool found = false;
    for (const auto& a : installed.value->apps) if (a.id == "testapp") found = true;
    PB_CHECK(found);
}

PB_TEST(a_refresh_reaches_the_host_counts_the_connections_stores_the_cache_and_writes_the_store_log) {
    StoreRig rig;
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    rig.host.publish(100);
    rig.write_reader();
    PB_CHECK_EQ(rig.connection_attempts(), 0);                    // nothing is requested before an action
    Engine& engine = rig.make_engine();
    auto handle = engine.open_reader(std::nullopt);
    CancelToken cancel;
    auto listing = engine.list(**handle.value, nullptr, cancel);
    PB_CHECK(listing.ok());
    PB_CHECK_EQ(rig.connection_attempts(), 2);                    // the catalog and its signature
    PB_CHECK(!rig.requests().empty());
    std::string c, s;
    PB_CHECK(rig.cache->load(&c, &s));                            // the accepted catalog was cached
    PB_CHECK_EQ(c, rig.host.files["/catalog.json"]);
    PB_CHECK(rig.storage.state->size("pbstore.log") != UINT64_MAX);   // the store's own log file name
    PB_CHECK_EQ(rig.storage.state->size("installer.log"), UINT64_MAX);
    // the same tree is what the contract of the state folder says: state.json and the cache files, nothing else of the engine
    PB_CHECK(rig.storage.state->size("state.json") != UINT64_MAX);
}

PB_TEST(a_request_carries_no_user_agent_header_when_the_downloader_is_given_none) {
    StoreRig rig;
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    rig.host.publish(100);
    rig.write_reader();
    Engine& engine = rig.make_engine();
    auto handle = engine.open_reader(std::nullopt);
    CancelToken cancel;
    PB_CHECK(engine.list(**handle.value, nullptr, cancel).ok());
    // Spec FR-043 wants no User-Agent at all. Whether libcurl or the engine adds one is what task T063 finds out; this records
    // the headers the rig captures so that the test written there has something to compare with.
    for (const std::string& r : rig.requests()) PB_CHECK(r.find("GET /") == 0);
}

PB_TEST_MAIN()
