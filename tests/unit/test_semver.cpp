// T012: semantic versions for apps (MAJOR.MINOR.PATCH with optional pre-release).
#include "pbstore/semver.h"
#include "test_support.h"

using namespace pbstore;

PB_TEST(accepts_valid_versions) {
    for (const char* ok : {"1.2.3", "0.0.0", "10.20.30", "1.2.3-rc1", "1.2.3-alpha.1", "1.0.0-0.3.7", "1.0.0-x-y-z"}) {
        auto v = parse_semver(ok);
        PB_CHECK(v.has_value());
        PB_CHECK_EQ(to_string(*v), std::string(ok));
    }
}

PB_TEST(rejects_invalid_versions) {
    for (const char* bad : {"1.2", "1", "v1.2.3", "01.2.3", "1.02.3", "1.2.03", "1.2.3+build", "1.2.3-", "1.2.3-.a",
                            "1.2.3-a..b", "1.2.3-01", "", " 1.2.3", "1.2.3 ", "1.2.3.4", "a.b.c", "-1.2.3",
                            "1.2.3-\xc3\xa4", "99999999999999999999.0.0"}) {
        PB_CHECK(!parse_semver(bad).has_value());
    }
}

static int cmp(const char* a, const char* b) {
    auto x = parse_semver(a), y = parse_semver(b);
    if (!x || !y) throw pbtest::Failure{std::string("unparsable: ") + a + " / " + b};
    return compare(*x, *y);
}

PB_TEST(precedence_follows_semantic_versioning) {
    const char* order[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
                           "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0"};
    const int n = sizeof(order) / sizeof(order[0]);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const int c = cmp(order[i], order[j]);
            if (i < j) PB_CHECK(c < 0);
            else if (i > j) PB_CHECK(c > 0);
            else PB_CHECK(c == 0);
        }
}

PB_TEST(numeric_parts_compare_as_numbers) {
    PB_CHECK(cmp("1.10.0", "1.9.0") > 0);
    PB_CHECK(cmp("1.0.10", "1.0.9") > 0);
    PB_CHECK(cmp("10.0.0", "9.99.99") > 0);
    PB_CHECK(cmp("1.0.0-rc.10", "1.0.0-rc.9") > 0);
    PB_CHECK(cmp("1.0.0-rc.1", "1.0.0-rcx") < 0);  // numeric identifiers sort below alphanumeric ones
}

PB_TEST_MAIN()
