// T104: the progress line says "3.1 of 7.4 MB" and "file 12 of 80", and is redrawn at most once every 2000 ms (the time is passed in: a fake clock).
#include "pbstore/store/progress_text.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;

namespace {
Progress make(const std::string& phase, uint64_t done, uint64_t total, const std::string& unit, const std::string& text) {
    Progress p;
    p.phase = phase;
    p.done = done;
    p.total = total;
    p.unit = unit;
    p.text = text;
    return p;
}
const uint64_t kMiB = 1024 * 1024;
}  // namespace

PB_TEST(a_download_says_megabytes_with_one_decimal) {
    PB_CHECK_EQ(progress_line(make("downloading", 3 * kMiB + kMiB / 10, 7 * kMiB + kMiB * 4 / 10, "bytes", "Downloading the package")),
                std::string("Downloading the package (3.1 of 7.4 MB)"));
    PB_CHECK_EQ(progress_figure(make("downloading", 0, 8 * kMiB, "bytes", "")), std::string("0.0 of 8.0 MB"));
}

PB_TEST(a_small_download_says_kilobytes_so_that_the_numbers_are_not_all_zero) {
    PB_CHECK_EQ(progress_figure(make("downloading", 12 * 1024, 48 * 1024, "bytes", "")), std::string("12 of 48 KB"));
    PB_CHECK_EQ(progress_figure(make("downloading", 5 * 1024, 0, "bytes", "")), std::string("5 KB"));
    PB_CHECK_EQ(progress_figure(make("downloading", 2 * kMiB, 0, "bytes", "")), std::string("2.0 MB"));
}

PB_TEST(files_and_steps_are_counted_in_words) {
    PB_CHECK_EQ(progress_figure(make("applying", 12, 80, "files", "")), std::string("file 12 of 80"));
    PB_CHECK_EQ(progress_figure(make("applying", 3, 7, "steps", "")), std::string("step 3 of 7"));
    PB_CHECK_EQ(progress_figure(make("applying", 1, 0, "files", "")), std::string("1 file"));
    PB_CHECK_EQ(progress_figure(make("applying", 4, 0, "files", "")), std::string("4 files"));
}

PB_TEST(nothing_measurable_leaves_the_sentence_alone) {
    PB_CHECK_EQ(progress_line(make("finishing", 0, 0, "", "Finishing")), std::string("Finishing"));
    PB_CHECK_EQ(progress_line(make("checking", 0, 5, "", "Checking")), std::string("Checking (0 of 5)"));
}

PB_TEST(the_redraw_is_held_back_to_one_per_two_seconds_and_a_new_phase_starts_free) {
    PB_CHECK_EQ(kProgressMinIntervalMs, int64_t(2000));
    ProgressThrottle t;
    PB_CHECK(t.pass(1000));          // the first report is drawn
    PB_CHECK(!t.pass(1001));
    PB_CHECK(!t.pass(2999));
    PB_CHECK(t.pass(3000));          // exactly two seconds after the last drawn one
    PB_CHECK(!t.pass(4500));
    t.reset();                       // a new phase
    PB_CHECK(t.pass(4501));
    PB_CHECK(!t.pass(5000));
}

PB_TEST(the_first_report_is_drawn_even_at_time_zero) {
    ProgressThrottle t;
    PB_CHECK(t.pass(0));
    PB_CHECK(!t.pass(1999));
    PB_CHECK(t.pass(2000));
}

PB_TEST_MAIN()
