// The plan text of the engine is shown unchanged, but a long list of files is folded behind a control (the owner had to turn 39 pages to
// reach the consents of PB Office, 2026-10-29). The folding only removes the per-file lines; everything else stays.
#include "pbstore/store/plan_text.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {

std::string plan_with_files(int n) {
    std::string t = "Plan: Install PB Office (pboffice), 0.4.1\nSource: the catalog\n";
    t += "Files (" + std::to_string(n) + " to change, 9.0 MiB to write):\n";
    for (int i = 0; i < n; ++i) t += "  add     applications/pboffice-zoneinfo/zone" + std::to_string(i) + "  1.0 KiB  sha256 0123456789ab\n";
    t += "Menu entry: add \"PB Office\" (pboffice), group \"Apps\"\nData folder: none\nWarnings:\n  - E-INS-09: the model of the reader cannot be read\n";
    return t;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

}  // namespace

PB_TEST(a_long_list_of_files_is_folded_and_the_rest_of_the_plan_stays) {
    const PlanTextParts p = split_plan_files(plan_with_files(39));
    PB_CHECK_EQ(p.file_lines, size_t(39));
    PB_CHECK(!has(p.brief, "zone0"));
    PB_CHECK(!has(p.brief, "sha256"));
    PB_CHECK(has(p.brief, "Plan: Install PB Office"));
    PB_CHECK(has(p.brief, "Files (39 to change, 9.0 MiB to write):"));
    PB_CHECK(has(p.brief, "39 files"));
    PB_CHECK(has(p.brief, "Menu entry: add"));
    PB_CHECK(has(p.brief, "Warnings:\n  - E-INS-09"));               // a line that starts with two blanks but is no file line stays
    PB_CHECK_EQ(p.full, plan_with_files(39));                         // the full text is the engine's, unchanged
}

PB_TEST(a_short_list_is_not_folded) {
    const PlanTextParts p = split_plan_files(plan_with_files(5));
    PB_CHECK_EQ(p.file_lines, size_t(5));
    PB_CHECK_EQ(p.brief, p.full);
    const PlanTextParts edge = split_plan_files(plan_with_files(8));
    PB_CHECK_EQ(edge.brief, edge.full);                               // up to eight files are shown
    const PlanTextParts over = split_plan_files(plan_with_files(9));
    PB_CHECK(over.brief != over.full);
}

PB_TEST(keep_and_remove_lines_count_as_file_lines_and_a_text_without_files_is_unchanged) {
    std::string t = "Plan: Remove X\nFiles (0 to change):\n";
    for (int i = 0; i < 10; ++i) t += "  remove  applications/f" + std::to_string(i) + "  1 B  sha256 aa\n  keep    applications/k" + std::to_string(i) + "  1 B  sha256 bb\n";
    const PlanTextParts p = split_plan_files(t);
    PB_CHECK_EQ(p.file_lines, size_t(20));
    PB_CHECK(!has(p.brief, "applications/f0"));
    const PlanTextParts none = split_plan_files("Plan: Adopt X\nData folder: none\n");
    PB_CHECK_EQ(none.file_lines, size_t(0));
    PB_CHECK_EQ(none.brief, none.full);
}

PB_TEST_MAIN()
