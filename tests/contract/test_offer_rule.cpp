// T028: an app is offered only if its version is not withdrawn.
#include "pbstore/catalog.h"
#include "sample_catalog.h"
#include "test_support.h"

using namespace pbstore;

PB_TEST(an_app_is_offered_unless_its_version_is_withdrawn) {
    CatalogEntry e = pbtest::sample_entry("alpha-app", "1.1.0");
    PB_CHECK(offered(e));
    e.withdrawn_versions = {"1.0.0"};          // an older version withdrawn: still offered
    PB_CHECK(offered(e));
    e.withdrawn_versions = {"1.0.0", "1.1.0"};
    PB_CHECK(!offered(e));
    e.withdrawn_versions = {"1.1.0"};
    PB_CHECK(!offered(e));
    e.withdrawn_versions.clear();
    PB_CHECK(offered(e));
}

PB_TEST(an_empty_catalog_is_valid_and_offers_nothing) {
    Catalog c = pbtest::sample_catalog();
    c.apps.clear();
    CatalogParseResult r = parse_catalog(write_catalog(c));
    PB_CHECK(r.catalog.has_value());
    PB_CHECK(r.catalog->apps.empty());
    size_t offered_count = 0;
    for (const CatalogEntry& e : r.catalog->apps) offered_count += offered(e) ? 1 : 0;
    PB_CHECK_EQ(offered_count, size_t(0));
}

PB_TEST_MAIN()
