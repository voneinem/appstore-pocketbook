// T037: when the panel gets a full refresh (research R-13, contracts/ui-pages.md): once at start, when the kind of page
// changes, after every 10th page turn, and on leaving. Never for a repeated page of the same kind.
#include "pbstore/store/refresh_policy.h"
#include "test_support.h"

using namespace pbstore::store;

PB_TEST(the_interval_is_ten_page_turns) {
    PB_CHECK_EQ(kPageTurnsPerFullRefresh, 10);
}

PB_TEST(a_full_refresh_is_asked_for_once_at_start) {
    RefreshPolicy p;
    PB_CHECK(p.start());
    PB_CHECK(!p.start());
    PB_CHECK(!p.show(PageKind::List));                              // the first page after the start is covered by the start
}

PB_TEST(a_change_of_the_kind_of_page_asks_a_repeat_of_the_same_kind_does_not) {
    RefreshPolicy p;
    p.start();
    p.show(PageKind::List);
    PB_CHECK(!p.show(PageKind::List));
    PB_CHECK(p.show(PageKind::Plan));
    PB_CHECK(!p.show(PageKind::Plan));
    PB_CHECK(p.show(PageKind::Progress));
    PB_CHECK(p.show(PageKind::Result));
    PB_CHECK(p.show(PageKind::List));
    PB_CHECK(!p.show(PageKind::List));
}

PB_TEST(every_tenth_page_turn_asks_and_the_counter_starts_again) {
    RefreshPolicy p;
    p.start();
    p.show(PageKind::List);
    for (int round = 0; round < 3; ++round) {
        for (int i = 1; i < kPageTurnsPerFullRefresh; ++i) PB_CHECK(!p.page_turn());
        PB_CHECK(p.page_turn());
    }
}

PB_TEST(a_full_refresh_for_another_reason_resets_the_page_turn_counter) {
    RefreshPolicy p;
    p.start();
    p.show(PageKind::List);
    for (int i = 0; i < 7; ++i) PB_CHECK(!p.page_turn());
    PB_CHECK(p.show(PageKind::Plan));                               // a full refresh: the count starts again
    p.show(PageKind::List);
    for (int i = 1; i < kPageTurnsPerFullRefresh; ++i) PB_CHECK(!p.page_turn());
    PB_CHECK(p.page_turn());
}

PB_TEST(leaving_asks_every_time) {
    RefreshPolicy p;
    PB_CHECK(p.leave());
    p.start();
    PB_CHECK(p.leave());
    PB_CHECK(p.leave());
}

PB_TEST_MAIN()
