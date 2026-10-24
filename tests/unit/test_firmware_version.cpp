// T013: firmware versions: dotted decimal numbers, at least two parts.
#include "pbstore/firmware_version.h"
#include "test_support.h"

using namespace pbstore;

static int cmp(const char* a, const char* b) {
    auto x = parse_firmware(a), y = parse_firmware(b);
    if (!x || !y) throw pbtest::Failure{std::string("unparsable: ") + a + " / " + b};
    return compare(*x, *y);
}

PB_TEST(accepts_dotted_numbers_with_two_or_more_parts) {
    for (const char* ok : {"6.8", "6.8.4143", "0.0", "6.8.4143.1", "100.200"}) PB_CHECK(parse_firmware(ok).has_value());
    PB_CHECK_EQ(to_string(*parse_firmware("6.8.4143")), std::string("6.8.4143"));
}

PB_TEST(rejects_malformed_versions) {
    for (const char* bad : {"6", "6.x", "6..8", "-1.2", "", ".6.8", "6.8.", "6.8 ", " 6.8", "v6.8", "6.8-beta",
                            "6.8.+1", "1234567890.1", "6.8.\xd9\xa1"}) {
        PB_CHECK(!parse_firmware(bad).has_value());
    }
}

PB_TEST(parts_larger_than_nine_digits_are_rejected) {
    PB_CHECK(parse_firmware("999999999.1").has_value());
    PB_CHECK(!parse_firmware("1000000000.1").has_value());
}

PB_TEST(compares_part_by_part_as_numbers) {
    PB_CHECK(cmp("6.10", "6.9") > 0);
    PB_CHECK(cmp("6.8.4143", "6.8.999") > 0);
    PB_CHECK(cmp("6.8", "6.9") < 0);
    PB_CHECK(cmp("7.0", "6.99.99") > 0);
}

PB_TEST(missing_trailing_parts_count_as_zero) {
    PB_CHECK(cmp("6.8", "6.8.0") == 0);
    PB_CHECK(cmp("6.8.0.0", "6.8") == 0);
    PB_CHECK(cmp("6.8.1", "6.8") > 0);
    PB_CHECK(cmp("6.8", "6.8.0.1") < 0);
}

PB_TEST_MAIN()
