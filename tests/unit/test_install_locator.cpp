// T037: finding the reader (spec FR-001, FR-004, FR-005): mounts and common folders are candidates; markers
// decide; several candidates are never reduced to one; plain findings for the failure cases.
#include "crash_fs.h"
#include "fake_mount_source.h"
#include "fake_reader.h"
#include "forwarding_fs.h"
#include "pbstore/install/locator.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using pbtest::CrashFs;
using pbtest::FakeMountSource;

namespace {
// A set of fake roots: each root path has its own in-memory volume.
struct World {
    FakeMountSource mounts;
    std::map<std::string, std::shared_ptr<CrashFs>> volumes;
    ReaderFsFactory factory() {
        return [this](const std::string& root) -> std::unique_ptr<ReaderFs> {
            auto it = volumes.find(root);
            if (it == volumes.end()) return std::make_unique<pbtest::ForwardingFs>(std::make_shared<CrashFs>());   // an empty folder
            return std::make_unique<pbtest::ForwardingFs>(it->second);
        };
    }
    void add_reader_volume(const std::string& root, bool valid = true) {
        auto fs = std::make_shared<CrashFs>();
        if (valid) pbtest::FakeReader().load_into(*fs);
        volumes[root] = fs;
    }
};
bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
}  // namespace

PB_TEST(only_fat_family_mounts_are_candidates) {
    World w;
    w.mounts.mount("/run/media/alice/PB1030", "vfat", 7ull << 30);
    w.mounts.mount("/run/media/alice/OTHER", "ext4", 100ull << 30);
    w.mounts.mount("/mnt/disk", "ntfs", 1ull << 30);
    w.mounts.mount("/media/alice/CARD", "exfat", 32ull << 30);
    w.mounts.mount("/mnt/old", "msdos", 1ull << 30);
    w.add_reader_volume("/run/media/alice/PB1030");
    w.add_reader_volume("/media/alice/CARD");
    w.add_reader_volume("/mnt/old");
    LocateResult r = locate(w.mounts, w.factory());
    std::vector<std::string> points;
    for (const auto& c : r.candidates) points.push_back(c.mount_point);
    std::sort(points.begin(), points.end());
    PB_CHECK_EQ(points, (std::vector<std::string>{"/media/alice/CARD", "/mnt/old", "/run/media/alice/PB1030"}));
}

PB_TEST(a_candidate_qualifies_only_with_both_markers_and_the_missing_part_is_named) {
    World w;
    w.mounts.mount("/run/media/alice/GOOD", "vfat", 1000);
    w.mounts.mount("/run/media/alice/NOAPPS", "vfat", 1000);
    w.mounts.mount("/run/media/alice/NOVIEW", "vfat", 1000);
    w.add_reader_volume("/run/media/alice/GOOD");
    {   // no applications/
        auto fs = std::make_shared<CrashFs>();
        fs->make_dir("system"); fs->make_dir("system/config"); fs->make_dir("system/config/desktop");
        fs->create_exclusive("system/config/desktop/view.json", "{}", true);
        w.volumes["/run/media/alice/NOAPPS"] = fs;
    }
    {   // no view.json
        auto fs = std::make_shared<CrashFs>();
        fs->make_dir("applications");
        w.volumes["/run/media/alice/NOVIEW"] = fs;
    }
    LocateResult r = locate(w.mounts, w.factory());
    PB_CHECK_EQ(r.candidates.size(), size_t(3));
    for (const auto& c : r.candidates) {
        if (c.mount_point.find("GOOD") != std::string::npos) { PB_CHECK(c.qualifies); PB_CHECK(c.missing.empty()); }
        if (c.mount_point.find("NOAPPS") != std::string::npos) {
            PB_CHECK(!c.qualifies);
            PB_CHECK_EQ(c.missing, (std::vector<std::string>{"applications/"}));
        }
        if (c.mount_point.find("NOVIEW") != std::string::npos) {
            PB_CHECK(!c.qualifies);
            PB_CHECK_EQ(c.missing, (std::vector<std::string>{"system/config/desktop/view.json"}));
        }
    }
}

PB_TEST(the_label_is_the_last_path_component_and_the_size_is_shown) {
    World w;
    w.mounts.mount("/run/media/alice/PB1030", "vfat", 7ull << 30);
    w.add_reader_volume("/run/media/alice/PB1030");
    LocateResult r = locate(w.mounts, w.factory());
    PB_CHECK_EQ(r.candidates.size(), size_t(1));
    PB_CHECK_EQ(r.candidates[0].label, std::string("PB1030"));
    PB_CHECK_EQ(r.candidates[0].mount_point, std::string("/run/media/alice/PB1030"));
    PB_CHECK_EQ(r.candidates[0].size_bytes, uint64_t(7) << 30);
}

PB_TEST(common_folders_are_a_second_source_and_are_not_listed_twice) {
    World w;
    w.mounts.mount("/run/media/alice/PB1030", "vfat", 1000);
    w.mounts.add_extra_root("/run/media/alice/PB1030");     // the same drive found by listing /run/media/alice
    w.mounts.add_extra_root("/media/alice/OTHERPB");        // a drive that /proc/self/mountinfo did not show us
    w.add_reader_volume("/run/media/alice/PB1030");
    w.add_reader_volume("/media/alice/OTHERPB");
    LocateResult r = locate(w.mounts, w.factory());
    PB_CHECK_EQ(r.candidates.size(), size_t(2));
}

PB_TEST(a_hand_chosen_folder_passes_the_same_test) {
    World w;
    w.add_reader_volume("/home/alice/copy-of-reader");
    Opened good = open_folder("/home/alice/copy-of-reader", w.factory());
    PB_CHECK(good.candidate.qualifies);
    PB_CHECK(good.findings.empty());
    PB_CHECK_EQ(good.candidate.label, std::string("copy-of-reader"));
    Opened bad = open_folder("/home/alice/not-a-reader", w.factory());
    PB_CHECK(!bad.candidate.qualifies);
    PB_CHECK(has_rule(bad.findings, "E-INS-03"));
    PB_CHECK(bad.findings[0].message.find("applications/") != std::string::npos);
    PB_CHECK(bad.findings[0].message.find("view.json") != std::string::npos);
}

PB_TEST(several_qualifying_candidates_are_never_reduced_to_one) {
    World w;
    w.mounts.mount("/run/media/alice/PB1", "vfat", 1000);
    w.mounts.mount("/run/media/alice/PB2", "vfat", 1000);
    w.add_reader_volume("/run/media/alice/PB1");
    w.add_reader_volume("/run/media/alice/PB2");
    LocateResult r = locate(w.mounts, w.factory());
    Pick p = pick_single_reader(r);
    PB_CHECK(!p.reader.has_value());
    PB_CHECK(has_rule(p.findings, "E-INS-02"));
    PB_CHECK(p.findings[0].message.find("PB1") != std::string::npos);
    PB_CHECK(p.findings[0].message.find("PB2") != std::string::npos);
    PB_CHECK(p.findings[0].message.find("--reader") != std::string::npos);
}

PB_TEST(exactly_one_qualifying_candidate_is_picked_even_next_to_a_non_qualifying_one) {
    World w;
    w.mounts.mount("/run/media/alice/PB1", "vfat", 1000);
    w.mounts.mount("/run/media/alice/STICK", "vfat", 1000);
    w.add_reader_volume("/run/media/alice/PB1");
    w.add_reader_volume("/run/media/alice/STICK", /*valid=*/false);
    Pick p = pick_single_reader(locate(w.mounts, w.factory()));
    PB_CHECK(p.reader.has_value());
    PB_CHECK(p.findings.empty());
    PB_CHECK_EQ(p.reader->label, std::string("PB1"));
}

PB_TEST(no_reader_found_gives_e_ins_01) {
    World w;
    LocateResult r = locate(w.mounts, w.factory());
    PB_CHECK(r.candidates.empty());
    Pick p = pick_single_reader(r);
    PB_CHECK(!p.reader.has_value());
    PB_CHECK(has_rule(p.findings, "E-INS-01"));
    PB_CHECK(!has_rule(p.findings, "E-INS-04"));
}

PB_TEST(a_pocketbook_on_usb_without_storage_gives_the_hint_e_ins_04) {
    World w;
    w.mounts.add_usb_manufacturer("Linux Foundation");
    w.mounts.add_usb_manufacturer("PocketBook");
    LocateResult r = locate(w.mounts, w.factory());
    Pick p = pick_single_reader(r);
    PB_CHECK(has_rule(p.findings, "E-INS-04"));
    PB_CHECK(p.findings[0].message.find("hint") != std::string::npos || p.findings[0].message.find("seems") != std::string::npos);
    // when a qualifying candidate exists the hint is not raised
    w.mounts.mount("/run/media/alice/PB1", "vfat", 1000);
    w.add_reader_volume("/run/media/alice/PB1");
    Pick q = pick_single_reader(locate(w.mounts, w.factory()));
    PB_CHECK(q.reader.has_value());
    PB_CHECK(q.findings.empty());
}

PB_TEST(an_mtp_mount_is_reported_as_not_supported) {
    World w;
    w.mounts.add_mtp_mount("/run/user/1000/gvfs/mtp:host=PocketBook_InkPad");
    LocateResult r = locate(w.mounts, w.factory());
    PB_CHECK(r.candidates.empty());
    PB_CHECK(!r.notes.empty());
    PB_CHECK(r.notes[0].find("MTP") != std::string::npos);
    PB_CHECK(r.notes[0].find("not supported") != std::string::npos);
    Pick p = pick_single_reader(r);
    PB_CHECK(has_rule(p.findings, "E-INS-04"));          // the same advice: switch to USB storage mode
    PB_CHECK(p.findings[0].message.find("MTP") != std::string::npos);
}

PB_TEST(a_read_only_or_unwritable_reader_is_found_before_any_plan) {
    CrashFs ro;
    pbtest::FakeReader().load_into(ro);
    ro.set_space(1ull << 30, 4096, /*writable=*/false, /*read_only=*/true);
    std::vector<Finding> f = check_writable(ro);
    PB_CHECK(has_rule(f, "E-INS-05"));
    PB_CHECK(!has_rule(f, "E-INS-06"));
    PB_CHECK(f[0].message.find("read-only") != std::string::npos);

    CrashFs noperm;
    pbtest::FakeReader().load_into(noperm);
    noperm.set_space(1ull << 30, 4096, false, false);
    std::vector<Finding> g = check_writable(noperm);
    PB_CHECK(has_rule(g, "E-INS-06"));
    PB_CHECK(!has_rule(g, "E-INS-05"));

    CrashFs fine;
    pbtest::FakeReader().load_into(fine);
    PB_CHECK(check_writable(fine).empty());
}

PB_TEST_MAIN()
