// T051: package_match, the one function the entry check and the store use to compare a package with
// a catalog entry (Principle III (2) and (3)).
#include "pbstore/package_match.h"
#include "pbstore/sha256.h"
#include "sample_app.h"
#include "sample_catalog.h"
#include "test_support.h"

using namespace pbstore;

static CatalogEntry entry_for(const std::string& zip, const std::string& id = "sample-app", const std::string& version = "1.2.0") {
    CatalogEntry e = pbtest::sample_entry(id, version);
    e.package_size = static_cast<int64_t>(zip.size());
    e.package_sha256 = sha256_hex(zip);
    e.menu_entry_id = "U_SampleApp";
    e.data_dir = "system/sample-app";
    e.min_firmware = "6.8.0";
    return e;
}
static bool has(const PackageMatchResult& r, const char* rule, const std::string& needle = "") {
    for (const Finding& f : r.findings)
        if (f.rule_id == rule && (f.message.find(needle) != std::string::npos || f.subject.find(needle) != std::string::npos)) return true;
    return false;
}

PB_TEST(a_matching_package_passes) {
    const std::string zip = pbtest::build_package(pbtest::make_sample_app());
    PackageMatchResult r = package_match(entry_for(zip), zip);
    PB_CHECK(r.ok());
    PB_CHECK(r.findings.empty());
    PB_CHECK(r.manifest.has_value());
}

PB_TEST(hash_or_size_that_differ_from_the_catalog_are_E_ENT_15) {
    const std::string zip = pbtest::build_package(pbtest::make_sample_app());
    CatalogEntry e = entry_for(zip);
    e.package_sha256 = std::string(64, 'a');
    PackageMatchResult a = package_match(e, zip);
    PB_CHECK(!a.ok() && has(a, "E-ENT-15", "SHA-256"));
    PB_CHECK(has(a, "E-ENT-15", e.package_sha256) && has(a, "E-ENT-15", sha256_hex(zip)));   // both values named
    CatalogEntry s = entry_for(zip);
    s.package_size += 1;
    PackageMatchResult b = package_match(s, zip);
    PB_CHECK(!b.ok() && has(b, "E-ENT-15", "size"));
    // a changed package is stopped before anything else is read from it
    PB_CHECK(!a.manifest.has_value());
}

PB_TEST(a_package_that_breaks_the_contract_is_reported_with_its_E_PKG_rule) {
    auto app = pbtest::make_sample_app();
    std::vector<pbtest::ZEntry> f = app.files;
    f.push_back(pbtest::file("applications/unlisted.txt", "x"));
    const std::string zip = pbtest::build_package(app.manifest, f);
    PackageMatchResult r = package_match(entry_for(zip), zip);
    PB_CHECK(!r.ok() && has(r, "E-PKG-11"));
}

PB_TEST(manifest_values_that_differ_from_the_catalog_entry_are_reported) {
    const std::string zip = pbtest::build_package(pbtest::make_sample_app());
    { CatalogEntry e = entry_for(zip, "other-app"); PackageMatchResult r = package_match(e, zip); PB_CHECK(!r.ok() && has(r, "E-ENT-13", "other-app") && has(r, "E-ENT-13", "sample-app")); }
    { CatalogEntry e = entry_for(zip); e.version = "1.3.0"; PackageMatchResult r = package_match(e, zip); PB_CHECK(!r.ok() && has(r, "E-ENT-17", "1.3.0") && has(r, "E-ENT-17", "1.2.0")); }
    { CatalogEntry e = entry_for(zip); e.min_firmware = "6.9.0"; PackageMatchResult r = package_match(e, zip); PB_CHECK(!r.ok() && has(r, "E-ENT-17", "min_firmware")); }
    { CatalogEntry e = entry_for(zip); e.menu_entry_id = "U_Other"; PackageMatchResult r = package_match(e, zip); PB_CHECK(!r.ok() && has(r, "E-ENT-17", "menu_entry")); }
    { CatalogEntry e = entry_for(zip); e.data_dir = "system/other"; PackageMatchResult r = package_match(e, zip); PB_CHECK(!r.ok() && has(r, "E-ENT-17", "data_dir")); }
    { CatalogEntry e = entry_for(zip); e.data_dir.reset(); PackageMatchResult r = package_match(e, zip); PB_CHECK(!r.ok() && has(r, "E-ENT-17", "data_dir")); }
}

PB_TEST_MAIN()
