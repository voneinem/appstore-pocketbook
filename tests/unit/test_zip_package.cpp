// T035: the bounded ZIP reader and the deterministic writer (docs/contracts/package-manifest.md).
#include "pbstore/sha256.h"
#include "pbstore/zip_package.h"
#include "sample_app.h"
#include "test_support.h"
#include "zip_builder.h"

using namespace pbstore;
using pbtest::file;

static std::vector<Finding> open_findings(const std::string& bytes, const ZipLimits& limits = ZipLimits()) {
    ZipArchive a;
    return a.open(bytes, limits);
}
static bool has(const std::vector<Finding>& f, const char* id) {
    for (const Finding& x : f) if (x.rule_id == id) return true;
    return false;
}
static std::vector<Finding> with_names(const std::vector<std::string>& names) {
    std::vector<pbtest::ZEntry> e;
    for (const std::string& n : names) e.push_back(file(n, "x"));
    return open_findings(pbtest::build_zip(e));
}

PB_TEST(a_valid_archive_opens_and_reads_back) {
    std::vector<pbtest::ZEntry> e = {file("manifest.json", "{}"), pbtest::directory("applications/"),
                                     file("applications/a.bin", pbtest::pseudo_bytes(1000, 3)),
                                     file("applications/dir/b.txt", "hello")};
    e[3].method = 0;   // stored and deflated both work
    ZipArchive a;
    PB_CHECK(a.open(pbtest::build_zip(e), ZipLimits()).empty());
    PB_CHECK_EQ(a.entries().size(), size_t(4));
    std::string content, error;
    for (const ZipEntry& z : a.entries()) {
        if (z.is_directory) continue;
        PB_CHECK(a.read(z, 1 << 20, &content, &error));
        for (const auto& src : e) if (src.name == z.name) PB_CHECK_EQ(content, src.data);
    }
    std::string hex; uint64_t size = 0;
    for (const ZipEntry& z : a.entries())
        if (z.name == "applications/dir/b.txt") {
            PB_CHECK(a.digest(z, &hex, &size, &error));
            PB_CHECK_EQ(hex, sha256_hex("hello"));
            PB_CHECK_EQ(size, uint64_t(5));
        }
}

PB_TEST(safe_names_are_accepted) {
    PB_CHECK(with_names({"manifest.json", "applications/pbnotes.app", "applications/icons/a b-c_d.1.bmp"}).empty());
    PB_CHECK(with_names({"applications/" + std::string(150, 'a')}).empty());
    // IANA time-zone names: "+" is valid on the reader's FAT storage and is part of real zone names
    PB_CHECK(with_names({"applications/zoneinfo/GMT+0", "applications/zoneinfo/Etc/GMT+12"}).empty());
}

PB_TEST(unsafe_names_are_rejected_with_the_entry_named) {
    const std::vector<std::string> bad = {
        "/abs.txt", "a//b.txt", "../x", "a/../b", "a/./b", ".hidden", "a/.b", "a\\b", "sp@ce", "caf\xc3\xa9",
        "a:b", "a*b", "a?b", "a|b", "a,b", "a;b", "a=b", "a&b", "a%b", "a#b", "tab\tname", std::string("nul\0name", 8), "applications/" + std::string(200, 'a')};
    for (const std::string& n : bad) {
        auto f = with_names({n});
        PB_CHECK(has(f, "E-PKG-03"));
        PB_CHECK(f[0].subject.find("applications/") == 0 || f[0].subject.size() > 0);
    }
    auto f = with_names({"../x"});
    PB_CHECK(f[0].message.find("../x") != std::string::npos || f[0].subject.find("../x") != std::string::npos);
}

PB_TEST(names_must_be_unique_ignoring_letter_case) {
    PB_CHECK(has(with_names({"applications/a.txt", "applications/A.txt"}), "E-PKG-03"));
    PB_CHECK(has(with_names({"applications/a.txt", "applications/a.txt"}), "E-PKG-03"));
    PB_CHECK(has(with_names({"applications/x", "APPLICATIONS/x"}), "E-PKG-03"));
    std::vector<pbtest::ZEntry> e = {pbtest::directory("applications/lib"), file("applications/lib", "file")};
    PB_CHECK(has(open_findings(pbtest::build_zip(e)), "E-PKG-03"));   // a directory and a file with one name
}

PB_TEST(symbolic_links_encryption_and_odd_compression_are_rejected) {
    PB_CHECK(has(open_findings(pbtest::build_zip({pbtest::symlink("applications/l", "/etc/passwd")})), "E-PKG-03"));
    pbtest::ZEntry enc = file("applications/e", "data");
    enc.flags = 1;
    PB_CHECK(has(open_findings(pbtest::build_zip({enc})), "E-PKG-01"));
    pbtest::ZEntry odd = file("applications/o", "data");
    odd.method = 12;
    PB_CHECK(has(open_findings(pbtest::build_zip({odd})), "E-PKG-01"));
    pbtest::ZEntry stored = file("applications/s", "data");
    stored.method = 0;
    PB_CHECK(open_findings(pbtest::build_zip({stored})).empty());
}

PB_TEST(limits_are_enforced_with_the_value_in_the_message) {
    // entries: 1000 allowed, 1001 not
    std::vector<pbtest::ZEntry> e;
    for (int i = 0; i < 1000; ++i) e.push_back(file("applications/f" + std::to_string(i), "x"));
    PB_CHECK(open_findings(pbtest::build_zip(e)).empty());
    e.push_back(file("applications/one-more", "x"));
    auto f = open_findings(pbtest::build_zip(e));
    PB_CHECK(has(f, "E-PKG-02"));
    PB_CHECK(f[0].message.find("1000") != std::string::npos);
    // compressed size of the whole package (custom limit keeps the test small)
    ZipLimits small;
    small.max_compressed_bytes = 500;
    pbtest::ZEntry big = file("applications/big.bin", pbtest::pseudo_bytes(2000, 1));
    big.method = 0;
    PB_CHECK(has(open_findings(pbtest::build_zip({big}), small), "E-PKG-02"));
    // uncompressed total: declared sizes of 150 MiB twice exceed 200 MiB
    pbtest::ZEntry a = file("applications/a", "x"), b = file("applications/b", "x");
    a.declared_usize = 150u * 1024 * 1024;
    b.declared_usize = 150u * 1024 * 1024;
    PB_CHECK(has(open_findings(pbtest::build_zip({a, b})), "E-PKG-02"));
    // one entry of 201 MiB
    pbtest::ZEntry c = file("applications/c", "x");
    c.declared_usize = 201u * 1024 * 1024;
    PB_CHECK(has(open_findings(pbtest::build_zip({c})), "E-PKG-02"));
}

PB_TEST(a_compression_ratio_guard_stops_zip_bombs) {
    pbtest::ZEntry zeros = file("applications/zeros.bin", std::string(5 * 1024 * 1024, '\0'));
    auto f = open_findings(pbtest::build_zip({zeros}));
    PB_CHECK(has(f, "E-PKG-02"));
    PB_CHECK(f[0].message.find("ratio") != std::string::npos);
    // a small, highly compressible file is fine; so is large incompressible data
    PB_CHECK(open_findings(pbtest::build_zip({file("applications/small.txt", std::string(100000, 'a'))})).empty());
    PB_CHECK(open_findings(pbtest::build_zip({file("applications/rand.bin", pbtest::pseudo_bytes(2 * 1024 * 1024, 5))})).empty());
}

PB_TEST(declared_sizes_are_not_trusted) {
    // a deflate stream that expands to 4 MiB, declared as 100 bytes
    pbtest::ZEntry liar = file("applications/liar.bin", std::string(4 * 1024 * 1024, 'q'));
    liar.declared_usize = 100;
    liar.crc_override = 0;
    ZipArchive a;
    auto f = a.open(pbtest::build_zip({liar}), ZipLimits());
    if (f.empty()) {   // if the ratio guard did not catch it at open, reading must refuse
        std::string out, error;
        PB_CHECK(!a.read(a.entries()[0], 1 << 30, &out, &error));
        PB_CHECK(out.size() <= 100);
        std::string hex; uint64_t size = 0;
        PB_CHECK(!a.digest(a.entries()[0], &hex, &size, &error));
    }
    // reading is capped by the caller's limit too
    ZipArchive b;
    PB_CHECK(b.open(pbtest::build_zip({file("manifest.json", std::string(2000, 'm'))}), ZipLimits()).empty());
    std::string out, error;
    PB_CHECK(!b.read(b.entries()[0], 1000, &out, &error));
    PB_CHECK(b.read(b.entries()[0], 2000, &out, &error));
}

PB_TEST(truncated_and_corrupt_archives_fail_cleanly) {
    const std::string good = pbtest::build_zip({file("manifest.json", "{\"a\":1}"), file("applications/a.bin", pbtest::pseudo_bytes(300, 9))});
    for (size_t n = 0; n < good.size(); n += 7) {
        ZipArchive a;
        auto f = a.open(good.substr(0, n), ZipLimits());
        if (n < good.size()) PB_CHECK(!f.empty());
    }
    PB_CHECK(has(open_findings(""), "E-PKG-01"));
    PB_CHECK(has(open_findings("PK\x05\x06 not really"), "E-PKG-01"));
}

PB_TEST(mutating_every_byte_never_crashes_and_never_returns_wrong_content) {
    const std::vector<pbtest::ZEntry> src = {file("manifest.json", "{\"id\":\"x\"}"), file("applications/a.bin", pbtest::pseudo_bytes(200, 4))};
    const std::string good = pbtest::build_zip(src);
    for (size_t i = 0; i < good.size(); ++i) {
        std::string m = good;
        m[i] = static_cast<char>(m[i] ^ 0x5A);
        ZipArchive a;
        if (!a.open(m, ZipLimits()).empty()) continue;           // rejected
        for (const ZipEntry& z : a.entries()) {
            if (z.is_directory) continue;
            std::string content, error;
            if (!a.read(z, 1 << 20, &content, &error)) continue; // read refused (CRC, size, stream)
            bool known = false;
            for (const auto& s : src) if (s.data == content) known = true;
            PB_CHECK(known);                                     // any content returned is original content
        }
    }
}

PB_TEST(the_writer_is_deterministic_and_sorted) {
    std::vector<ZipFile> a = {{"applications/b.bin", pbtest::pseudo_bytes(500, 1)}, {"manifest.json", "{}"}, {"applications/a.txt", "hello hello hello hello"}};
    std::vector<ZipFile> b = {a[2], a[0], a[1]};   // other order
    const std::string x = write_zip(a), y = write_zip(b);
    PB_CHECK_EQ(x, y);
    PB_CHECK_EQ(write_zip(a), x);
    ZipArchive z;
    PB_CHECK(z.open(x, ZipLimits()).empty());
    PB_CHECK_EQ(z.entries().size(), size_t(3));
    PB_CHECK_EQ(z.entries()[0].name, std::string("applications/a.txt"));   // byte order
    PB_CHECK_EQ(z.entries()[1].name, std::string("applications/b.bin"));
    PB_CHECK_EQ(z.entries()[2].name, std::string("manifest.json"));
    std::string content, error;
    PB_CHECK(z.read(z.entries()[1], 1 << 20, &content, &error));
    PB_CHECK_EQ(content, pbtest::pseudo_bytes(500, 1));
    PB_CHECK_EQ(write_zip({}).size() > 0, true);   // an empty archive is still a valid archive
}

PB_TEST_MAIN()
