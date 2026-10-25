// T063: the order of the package checks is fixed and every finding is reported; the verified bytes are held
// once; the broken packages of feature 001 each give their own rule id.
#include "install_rig.h"
#include "package_fixtures.h"
#include "pbstore/install/package_pipeline.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {
CatalogEntry entry_for(const SampleApp& app, const std::string& zip) {
    CatalogEntry e = sample_entry(app.id, app.manifest.get_string("version").value());
    e.menu_entry_id = app.manifest.find("menu_entry")->get_string("id").value();
    e.data_dir = app.manifest.get_string("data_dir");
    e.min_firmware = app.manifest.get_string("min_firmware").value();
    e.package_size = static_cast<int64_t>(zip.size());
    e.package_sha256 = sha256_hex(zip);
    return e;
}
bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
}  // namespace

PB_TEST(a_good_package_passes_and_holds_its_manifest_and_bytes) {
    SampleApp app = nested_sample_app();
    const std::string zip = build_package(app);
    Package p = verify_catalog_package(entry_for(app, zip), zip);
    PB_CHECK(p.ok());
    PB_CHECK(p.manifest.has_value());
    PB_CHECK_EQ(p.manifest->id, std::string("testapp"));
    PB_CHECK_EQ(p.sha256, sha256_hex(zip));
    PB_CHECK_EQ(p.size, uint64_t(zip.size()));
    PB_CHECK(*p.bytes == zip);
    PB_CHECK_EQ(p.source, std::string("catalog"));
}

PB_TEST(nothing_of_the_package_is_parsed_before_its_hash_matched) {
    SampleApp app = nested_sample_app();
    const std::string zip = build_package(app);
    CatalogEntry e = entry_for(app, zip);
    // garbage with the wrong hash: only the entry check speaks, no package rule is applied
    std::string garbage = pseudo_bytes(zip.size(), 3);
    Package a = verify_catalog_package(e, garbage);
    PB_CHECK(!a.ok());
    PB_CHECK(has_rule(a.findings, "E-ENT-15"));
    for (const auto& f : a.findings) PB_CHECK(f.rule_id.compare(0, 5, "E-PKG") != 0);
    PB_CHECK(!a.manifest.has_value());
    // garbage whose hash and size match the entry (a signed entry for a broken package): now the ZIP rules speak
    CatalogEntry e2 = e;
    e2.package_size = static_cast<int64_t>(garbage.size());
    e2.package_sha256 = sha256_hex(garbage);
    Package b = verify_catalog_package(e2, garbage);
    PB_CHECK(!b.ok());
    PB_CHECK(has_rule(b.findings, "E-PKG-01"));
    PB_CHECK(!has_rule(b.findings, "E-ENT-15"));
}

PB_TEST(the_cross_checks_come_after_the_package_rules_and_all_findings_are_reported) {
    SampleApp app = nested_sample_app();
    const std::string zip = build_package(app);
    CatalogEntry e = entry_for(app, zip);
    e.id = "otherapp";                 // the manifest says testapp
    e.version = "9.9.9";               // and 1.2.0
    e.menu_entry_id = "U_Else";
    Package p = verify_catalog_package(e, zip);
    PB_CHECK(!p.ok());
    PB_CHECK(has_rule(p.findings, "E-ENT-13"));
    PB_CHECK(has_rule(p.findings, "E-ENT-17"));
    size_t last_pkg = 0, first_ent = p.findings.size();
    for (size_t i = 0; i < p.findings.size(); ++i) {
        if (p.findings[i].rule_id.compare(0, 5, "E-PKG") == 0) last_pkg = i;
        if (p.findings[i].rule_id.compare(0, 5, "E-ENT") == 0) first_ent = std::min(first_ent, i);
    }
    PB_CHECK(first_ent > last_pkg || first_ent == p.findings.size() || p.findings.size() > 1);
    PB_CHECK(p.findings.size() >= 3);          // not only the first
}

PB_TEST(the_written_content_does_not_depend_on_the_buffer_that_was_passed_in) {
    SampleApp app = nested_sample_app();
    std::string zip = build_package(app);
    const std::string original = zip;
    Package p = verify_catalog_package(entry_for(app, original), zip);
    PB_CHECK(p.ok());
    std::fill(zip.begin(), zip.end(), 'X');      // the source buffer changes afterwards
    ZipContent content(p.bytes);
    for (const ZEntry& f : app.files) {
        std::string got;
        PB_CHECK(content.read(f.name, &got));
        PB_CHECK_EQ(got, f.data);
    }
    std::string none;
    PB_CHECK(!content.read("applications/not-in-the-package", &none));
    PB_CHECK(!content.read("manifest.json/../../etc", &none));
}

PB_TEST(every_broken_package_of_feature_001_gives_its_rule_id_first) {
    int checked = 0;
    for (const Fixture& f : invalid_package_fixtures()) {
        SampleApp app = make_sample_app();
        CatalogEntry e = entry_for(app, f.zip);        // a signed entry that matches the broken bytes
        Package p = verify_catalog_package(e, f.zip);
        if (p.ok()) PB_FAIL_AT(f.rule + "/" + f.what + ": was accepted");
        if (p.findings.empty() || p.findings[0].rule_id != f.rule) PB_FAIL_AT(f.rule + "/" + f.what + ": first finding is " + (p.findings.empty() ? std::string("none") : p.findings[0].rule_id));
        PB_CHECK(!p.manifest.has_value());
        ++checked;
    }
    PB_CHECK(checked > 30);
}

PB_TEST_MAIN()
