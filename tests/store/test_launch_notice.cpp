// The notices of the launcher's GPU wait (contracts/launcher.md S7): plain sentences, what is happening and what to do, no error code.
#include "pbstore/store/launch_notice.h"
#include "test_support.h"

using namespace pbstore::store;

PB_TEST(the_waiting_notice_says_to_wait_and_that_the_store_starts_by_itself) {
    const auto n = launch_notice("waiting");
    PB_CHECK(n.has_value());
    PB_CHECK(n->text.find("Please wait") != std::string::npos);
    PB_CHECK(n->text.find("starts by itself") != std::string::npos);
    PB_CHECK(n->text.find("two minutes") != std::string::npos);
}

PB_TEST(the_final_notice_says_what_to_do_and_that_nothing_is_wrong_with_the_store) {
    const auto n = launch_notice("gave-up");
    PB_CHECK(n.has_value());
    PB_CHECK(n->text.find("tap PB Store again") != std::string::npos);
    PB_CHECK(n->text.find("restart the reader") != std::string::npos);
    PB_CHECK(n->text.find("Nothing is wrong") != std::string::npos);
}

PB_TEST(the_notices_carry_no_error_code_and_an_unknown_kind_has_none) {
    for (const char* kind : {"waiting", "gave-up"}) {
        const auto n = launch_notice(kind);
        PB_CHECK(n.has_value());
        PB_CHECK(!n->title.empty());
        PB_CHECK(n->title.find("E-") == std::string::npos && n->text.find("E-") == std::string::npos);
    }
    PB_CHECK(!launch_notice("").has_value());
    PB_CHECK(!launch_notice("something").has_value());
}
PB_TEST_MAIN()
