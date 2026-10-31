// T024: the reader's configuration of the engine's operating-system interfaces (feature 003, contracts/device-interfaces.md)
// meets the same contract cases as the computer's, plus the engine can open it. This test also stands for engine gap E3
// (Engine::readers() dereferences Deps::mounts, so the store supplies a MountSource of one mount).
#include "crash_fs.h"
#include "fake_reader.h"
#include "fat_rules_fs.h"
#include "fixed_clock.h"
#include "mount_source_contract.h"
#include "pbstore/install/engine.h"
#include "pbstore/install/posix_reader_fs.h"
#include "pbstore/store/reader_storage.h"
#include "reader_fs_contract.h"
#include "state_dirs_contract.h"
#include "test_support.h"
#include "tmp_dir.h"
#include "tree_compare.h"

#include <sys/stat.h>

using namespace pbstore::install;
using namespace pbstore::store;

namespace {

// A FatRulesFs that owns the file system it wraps, so that a factory can return it.
class OwningFatRules : public pbtest::FatRulesFs {
public:
    explicit OwningFatRules(std::unique_ptr<ReaderFs> base) : FatRulesFs(*base), owned_(std::move(base)) {}

private:
    std::unique_ptr<ReaderFs> owned_;
};

ReaderFsFactory reader_factory() {
    return [](const std::string& root) -> std::unique_ptr<ReaderFs> { return make_reader_storage(root).open_fs(root); };
}

}  // namespace

PB_TEST(the_readers_file_system_meets_the_reader_fs_contract) {
    pbtest::run_reader_fs_contract(reader_factory(), "reader configuration");
}

PB_TEST(the_readers_file_system_wrapped_in_fat_rules_meets_the_reader_fs_contract) {
    pbtest::run_reader_fs_contract(
        [](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<OwningFatRules>(make_reader_storage(root).open_fs(root)); },
        "reader configuration over FatRulesFs");
}

PB_TEST(the_in_memory_crash_volume_meets_the_reader_fs_contract) {
    pbtest::run_reader_fs_contract([](const std::string&) -> std::unique_ptr<ReaderFs> { return std::make_unique<pbtest::CrashFs>(); }, "CrashFs");
}

PB_TEST(the_state_folder_is_system_pbstore_app_and_meets_the_state_dirs_contract) {
    pbtest::TmpDir tmp;
    const std::string root = tmp.path() + "/mnt/ext1";
    ReaderStorage storage = make_reader_storage(root);
    PB_CHECK_EQ(storage.root, root);
    PB_CHECK_EQ(storage.state->state_dir(), root + "/system/pbstore/app");
    pbtest::run_state_dirs_contract(*storage.state);
    // the contract created nothing outside system/pbstore/app (its parents are directories only)
    const std::string app = "system/pbstore/app";
    for (const auto& item : pbtest::snapshot(root)) {
        const bool inside = item.path == app || item.path.compare(0, app.size() + 1, app + "/") == 0;
        const bool parent = item.kind == 'd' && (item.path == "system" || item.path == "system/pbstore");
        PB_CHECK(inside || parent);
    }
}

PB_TEST(the_mount_source_has_exactly_one_vfat_mount_at_the_root_it_was_given) {
    ReaderStorage storage = make_reader_storage("/mnt/ext1");
    pbtest::run_mount_source_contract(*storage.mounts);
    auto mounts = storage.mounts->mounts();
    PB_CHECK_EQ(mounts.size(), size_t(1));
    PB_CHECK_EQ(mounts[0].mount_point, std::string("/mnt/ext1"));
    PB_CHECK_EQ(mounts[0].fstype, std::string("vfat"));
    PB_CHECK(storage.mounts->extra_roots().empty());
    PB_CHECK(storage.mounts->usb_manufacturers().empty());
    PB_CHECK(storage.mounts->mtp_mounts().empty());
    PB_CHECK_EQ(make_reader_storage().root, std::string("/mnt/ext1"));   // the default is the reader's storage
}

PB_TEST(the_engine_opens_the_reader_and_finds_that_one_candidate) {
    pbtest::TmpDir tmp;
    const std::string root = tmp.path() + "/mnt/ext1";
    ::mkdir((tmp.path() + "/mnt").c_str(), 0755);                // FakeReader::write_to makes one level
    pbtest::FakeReader().write_to(root);
    ReaderStorage storage = make_reader_storage(root);
    pbtest::FixedClock clock;
    Redactor redactor;
    Log log(*storage.state, clock, redactor, "pbstore.log");
    Config config;
    config.catalog_url = "https://example.invalid/catalog.json";
    Deps deps;
    deps.open_fs = storage.open_fs;
    deps.mounts = storage.mounts.get();
    deps.state = storage.state.get();
    deps.clock = &clock;
    deps.log = &log;
    deps.redactor = &redactor;
    Engine engine(config, deps);
    LocateResult located = engine.readers();                 // would crash if Deps::mounts were null (gap E3)
    PB_CHECK_EQ(located.candidates.size(), size_t(1));
    PB_CHECK(located.candidates[0].qualifies);
    PB_CHECK_EQ(located.candidates[0].mount_point, root);
    auto handle = engine.open_reader(std::nullopt);
    PB_CHECK(handle.ok());
    auto direct = engine.open_reader(root);
    PB_CHECK(direct.ok());
}

PB_TEST_MAIN()
