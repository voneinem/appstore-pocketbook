// The free space an operation needs (spec FR-002): the formula is used verbatim.
#include "pbstore/install/space.h"
#include "test_support.h"

using namespace pbstore::install;

PB_TEST(files_are_rounded_up_to_whole_clusters) {
    PB_CHECK_EQ(round_to_clusters(0, 4096), uint64_t(0));
    PB_CHECK_EQ(round_to_clusters(1, 4096), uint64_t(4096));
    PB_CHECK_EQ(round_to_clusters(4096, 4096), uint64_t(4096));
    PB_CHECK_EQ(round_to_clusters(4097, 4096), uint64_t(8192));
    PB_CHECK_EQ(round_to_clusters(10000, 32768), uint64_t(32768));
}

PB_TEST(the_sum_has_the_parts_of_the_formula) {
    SpaceInput in;
    in.cluster = 4096;
    in.new_file_sizes = {1, 4096, 5000};                 // 4096 + 4096 + 8192
    in.desktop_config_size = 3000;                        // the backup: 4096
    in.new_desktop_config_size = 3100;                    // the prepared copy: 4096 (journal)
    in.record_size = 900;                                 // 4096, and its staged copy 4096
    in.intent_size = 5000;                                // 8192 and two markers of one cluster each
    in.directories_to_create = 2;                         // one cluster each
    SpaceParts p = space_needed(in);
    PB_CHECK_EQ(p.files, uint64_t(16384));
    PB_CHECK_EQ(p.staging, uint64_t(16384));
    PB_CHECK_EQ(p.backup, uint64_t(4096));
    PB_CHECK_EQ(p.record, uint64_t(8192));
    PB_CHECK_EQ(p.journal, uint64_t(8192 + 2 * 4096 + 4096));
    PB_CHECK_EQ(p.directories, uint64_t(8192));
    PB_CHECK_EQ(p.old_version, uint64_t(0));
    PB_CHECK_EQ(p.margin, uint64_t(10 * 1024 * 1024));
    PB_CHECK_EQ(p.total(), p.files + p.staging + p.backup + p.record + p.journal + p.directories + p.margin);
}

PB_TEST(an_update_counts_the_old_version_too_until_the_commit_point) {
    SpaceInput in;
    in.cluster = 4096;
    in.new_file_sizes = {4096};
    in.old_file_sizes = {4096, 100};
    SpaceParts p = space_needed(in);
    PB_CHECK_EQ(p.old_version, uint64_t(8192));
}

PB_TEST(nothing_is_counted_for_a_desktop_configuration_that_does_not_change) {
    SpaceInput in;
    in.cluster = 4096;
    in.record_size = 100;
    SpaceParts p = space_needed(in);
    PB_CHECK_EQ(p.backup, uint64_t(0));
    PB_CHECK_EQ(p.margin, kSpaceMarginBytes);
}

PB_TEST_MAIN()
