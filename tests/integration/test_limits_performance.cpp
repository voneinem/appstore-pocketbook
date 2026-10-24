// T086: packages exactly at each limit are accepted (and checked fast), one byte or entry over is E-PKG-02.
#include "pbstore/package_check.h"
#include "pbstore/zip_package.h"
#include "sample_app.h"
#include "test_support.h"
#include "zip_builder.h"

#include <chrono>

using namespace pbstore;
using pbtest::file;

static double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// a package whose big stored file is `n` bytes
static std::string package_with_big_file(size_t n) {
    pbtest::SampleApp app = pbtest::make_sample_app();
    pbtest::ZEntry big = file("applications/big.bin", pbtest::pseudo_bytes(n, 7));
    big.method = 0;
    std::vector<pbtest::ZEntry> files = app.files;
    files.push_back(big);
    pbtest::set_member(app.manifest, "files", pbtest::files_value(files));
    // the manifest is stored, not deflated, so the overhead is the same for every n
    pbtest::ZEntry manifest = file("manifest.json", pbstore::json::write(app.manifest));
    manifest.method = 0;
    std::vector<pbtest::ZEntry> all = {manifest};
    all.insert(all.end(), files.begin(), files.end());
    return pbtest::build_zip(all);
}

PB_TEST(a_package_of_exactly_64_MiB_is_accepted_quickly_and_one_byte_more_is_not) {
    const size_t limit = 64u * 1024 * 1024;
    size_t n = limit - 200000;
    std::string zip = package_with_big_file(n);
    n += limit - zip.size();                        // the overhead is constant: adjust to hit the limit exactly
    zip = package_with_big_file(n);
    PB_CHECK_EQ(zip.size(), limit);
    const auto t0 = std::chrono::steady_clock::now();
    PackageCheckResult r = check_package(zip);
    PB_CHECK(r.ok());
    PB_CHECK(seconds_since(t0) < 30.0);
    std::printf("         64 MiB package checked in %.2f s\n", seconds_since(t0));
    std::string over = package_with_big_file(n + 1);
    PB_CHECK_EQ(over.size(), limit + 1);
    PackageCheckResult o = check_package(over);
    PB_CHECK(!o.ok());
    PB_CHECK_EQ(o.findings[0].rule_id, std::string("E-PKG-02"));
    PB_CHECK(o.findings[0].message.find("67108864") != std::string::npos);
}

PB_TEST(a_package_of_exactly_1000_entries_is_accepted_and_1001_is_not) {
    pbtest::SampleApp app = pbtest::make_sample_app();
    std::vector<pbtest::ZEntry> files = app.files;
    while (files.size() + 1 < 1000) files.push_back(file("applications/extra/f" + std::to_string(files.size()) + ".txt", "x"));
    pbtest::set_member(app.manifest, "files", pbtest::files_value(files));
    std::string zip = pbtest::build_package(app.manifest, files);       // manifest.json + 999 files = 1000 entries
    PB_CHECK(check_package(zip).ok());
    files.push_back(file("applications/extra/one-more.txt", "x"));
    pbtest::set_member(app.manifest, "files", pbtest::files_value(files));
    PackageCheckResult r = check_package(pbtest::build_package(app.manifest, files));
    PB_CHECK(!r.ok());
    PB_CHECK_EQ(r.findings[0].rule_id, std::string("E-PKG-02"));
}

PB_TEST(entries_that_expand_to_exactly_200_MiB_are_accepted_and_one_byte_more_is_not) {
    const size_t limit = 200u * 1024 * 1024;
    const size_t chunk = 1024 * 1024 - 1;       // below the size the ratio guard applies to
    auto build = [&](size_t total) {
        std::vector<pbtest::ZEntry> entries;
        size_t left = total;
        for (int i = 0; left > 0; ++i) {
            const size_t n = std::min(chunk, left);
            entries.push_back(file("applications/z" + std::to_string(i) + ".bin", std::string(n, '\0')));
            left -= n;
        }
        return pbtest::build_zip(entries);
    };
    const auto t0 = std::chrono::steady_clock::now();
    const std::string at_limit = build(limit);
    ZipArchive a;
    PB_CHECK(a.open(at_limit, ZipLimits()).empty());
    std::printf("         200 MiB of entries opened in %.2f s\n", seconds_since(t0));
    ZipArchive b;
    auto over = b.open(build(limit + 1), ZipLimits());
    PB_CHECK(!over.empty() && over[0].rule_id == "E-PKG-02");
    PB_CHECK(seconds_since(t0) < 60.0);
}

PB_TEST_MAIN()
