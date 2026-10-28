// T044: the guess "the reader's clock looks wrong" (research R-10, spec Edge Cases). It is made only after a TLS failure,
// changes only the wording of a message, and is never an input to a check.
#include "pbstore/store/clock_check.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::store;

namespace {
install::Utc utc(int y, int mo, int d) {
    install::Utc u;
    u.year = y; u.month = mo; u.day = d; u.hour = 12;
    return u;
}
}  // namespace

PB_TEST(a_tls_failure_with_a_clock_before_the_release_date_looks_wrong) {
    PB_CHECK(clock_looks_wrong(utc(2026, 10, 27), "2026-10-28", std::nullopt, FetchKind::Tls));
    PB_CHECK(!clock_looks_wrong(utc(2026, 10, 28), "2026-10-28", std::nullopt, FetchKind::Tls));   // the release day itself is fine
    PB_CHECK(!clock_looks_wrong(utc(2027, 3, 1), "2026-10-28", std::nullopt, FetchKind::Tls));
}

PB_TEST(a_clock_before_the_generation_date_of_the_cached_catalog_looks_wrong) {
    PB_CHECK(clock_looks_wrong(utc(2026, 11, 1), "2026-10-28", std::string("2026-11-02T08:00:00Z"), FetchKind::Tls));
    PB_CHECK(!clock_looks_wrong(utc(2026, 11, 3), "2026-10-28", std::string("2026-11-02T08:00:00Z"), FetchKind::Tls));
    PB_CHECK(!clock_looks_wrong(utc(2026, 11, 3), "2026-10-28", std::string("garbled"), FetchKind::Tls));   // unreadable: only the release date counts
}

PB_TEST(a_year_below_2024_or_above_2100_looks_wrong_even_without_a_known_floor) {
    PB_CHECK(clock_looks_wrong(utc(1970, 1, 1), "", std::nullopt, FetchKind::Tls));
    PB_CHECK(clock_looks_wrong(utc(2023, 12, 31), "", std::nullopt, FetchKind::Tls));
    PB_CHECK(!clock_looks_wrong(utc(2024, 1, 1), "", std::nullopt, FetchKind::Tls));
    PB_CHECK(!clock_looks_wrong(utc(2100, 12, 31), "", std::nullopt, FetchKind::Tls));
    PB_CHECK(clock_looks_wrong(utc(2101, 1, 1), "", std::nullopt, FetchKind::Tls));
    PB_CHECK(clock_looks_wrong(utc(2150, 6, 1), "2026-10-28", std::nullopt, FetchKind::Tls));
}

PB_TEST(a_failure_that_is_not_a_certificate_failure_never_blames_the_clock) {
    for (FetchKind k : {FetchKind::None, FetchKind::Unreachable, FetchKind::HttpStatus, FetchKind::TooLarge, FetchKind::Protocol, FetchKind::Timeout, FetchKind::Other})
        PB_CHECK(!clock_looks_wrong(utc(1970, 1, 1), "2026-10-28", std::nullopt, k));
}

PB_TEST(the_date_shown_is_yyyy_mm_dd_in_utc) {
    PB_CHECK_EQ(clock_date_text(utc(2026, 3, 9)), std::string("2026-03-09"));
    PB_CHECK_EQ(clock_date_text(utc(1970, 1, 1)), std::string("1970-01-01"));
}

PB_TEST(the_guess_has_no_other_inputs_than_the_clock_the_two_dates_and_the_failure_kind) {
    // A function of exactly these values: the same inputs give the same answer, and there is no way to pass a setting
    // that would skip, weaken or postpone the certificate check.
    for (int i = 0; i < 3; ++i) PB_CHECK(clock_looks_wrong(utc(2000, 1, 1), "2026-10-28", std::nullopt, FetchKind::Tls));
}

PB_TEST_MAIN()
