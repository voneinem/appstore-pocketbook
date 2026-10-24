// T037: one case per package rule E-PKG-01 … E-PKG-15 (SC-003), built with zip_builder.h.
#include "pbstore/package_check.h"
#include "pbstore/sha256.h"
#include "package_fixtures.h"
#include "test_support.h"

#include <set>

using namespace pbstore;

PB_TEST(a_conforming_package_passes_and_reports_its_size_and_hash) {
    auto app = pbtest::make_sample_app();
    const std::string zip = pbtest::build_package(app);
    PackageCheckResult r = check_package(zip);
    PB_CHECK(r.ok());
    PB_CHECK(r.findings.empty());
    PB_CHECK(r.manifest.has_value());
    PB_CHECK_EQ(r.package_size, static_cast<int64_t>(zip.size()));
    PB_CHECK_EQ(r.package_sha256, sha256_hex(zip));
    PB_CHECK_EQ(r.manifest->id, std::string("sample-app"));
}

PB_TEST(every_invalid_fixture_reports_its_rule_first_and_names_the_problem) {
    std::set<std::string> covered;
    for (const pbtest::Fixture& fx : pbtest::invalid_package_fixtures()) {
        PackageCheckResult r = check_package(fx.zip);
        const std::string label = fx.rule + "/" + fx.what;
        if (r.ok()) throw pbtest::Failure{"accepted: " + label};
        if (r.findings.empty() || r.findings[0].rule_id != fx.rule) {
            std::string all;
            for (const Finding& f : r.findings) all += format_finding(f) + " | ";
            throw pbtest::Failure{"first finding is not " + fx.rule + " for " + label + ": [" + all + "]"};
        }
        PB_CHECK(!r.findings[0].message.empty() && !r.findings[0].subject.empty());
        covered.insert(fx.rule);
    }
    PB_CHECK_EQ(covered.size(), size_t(15));   // E-PKG-01 … E-PKG-15, none forgotten
}

static std::string first_message(const std::string& zip, const char* rule) {
    for (const Finding& f : check_package(zip).findings) if (f.rule_id == rule) return f.subject + " " + f.message;
    return "";
}

PB_TEST(messages_name_the_file_the_field_and_both_values) {
    for (const pbtest::Fixture& fx : pbtest::invalid_package_fixtures()) {
        const std::string m = first_message(fx.zip, fx.rule.c_str());
        if (fx.what == "sha-mismatch") { PB_CHECK(m.find("applications/sample-app.bin") != std::string::npos); PB_CHECK(m.find("manifest") != std::string::npos && m.find("actual") != std::string::npos); }
        if (fx.what == "size-mismatch") PB_CHECK(m.find("applications/icons/sample-app.bmp") != std::string::npos);
        if (fx.what == "missing-file") PB_CHECK(m.find("applications/sample-app.bin") != std::string::npos);
        if (fx.what == "unlisted-file") PB_CHECK(m.find("applications/unlisted.txt") != std::string::npos);
        if (fx.what == "extra-root-file") PB_CHECK(m.find("INSTALL.md") != std::string::npos);
        if (fx.what == "dotdot") PB_CHECK(m.find("..") != std::string::npos);
        if (fx.what == "menu-path-not-listed") PB_CHECK(m.find("applications/nothere.app") != std::string::npos);
        if (fx.what == "bad-id") PB_CHECK(m.find("Bad_Id") != std::string::npos);
        if (fx.what == "reserved-data-dir") PB_CHECK(m.find("system/config") != std::string::npos || m.find("config") != std::string::npos);
        if (fx.what == "newer-major") PB_CHECK(m.find("needs updating") != std::string::npos);
        if (fx.what == "too-many-entries") PB_CHECK(m.find("1000") != std::string::npos);
    }
}

PB_TEST(all_findings_are_collected_not_just_the_first) {
    // two independent problems in one package: a changed file and an unlisted file
    auto app = pbtest::make_sample_app();
    std::vector<pbtest::ZEntry> f = app.files;
    f[1].data[3] = static_cast<char>(f[1].data[3] ^ 1);
    f.push_back(pbtest::file("applications/unlisted.txt", "x"));
    PackageCheckResult r = check_package(pbtest::build_package(app.manifest, f));
    bool e11 = false, e12 = false;
    for (const Finding& x : r.findings) { e11 = e11 || x.rule_id == "E-PKG-11"; e12 = e12 || x.rule_id == "E-PKG-12"; }
    PB_CHECK(e11 && e12);
}

PB_TEST(a_package_with_a_directory_entries_and_extra_empty_directory_is_fine) {
    auto app = pbtest::make_sample_app();
    PackageCheckResult r = check_package(pbtest::build_package(app.manifest, app.files,
        {pbtest::directory("applications/"), pbtest::directory("applications/icons/"), pbtest::directory("applications/empty/")}));
    PB_CHECK(r.ok());
}

PB_TEST_MAIN()
