#pragma once
// Test helper: one or more anonymized invalid packages for every package rule (E-PKG-01 … E-PKG-15),
// built at test time. gen_fixtures.cpp writes them to a directory; the contract tests check that
// each one is reported with exactly its rule id first. No real app or reader data is used.

#include <string>
#include <vector>

#include "sample_app.h"

namespace pbtest {

struct Fixture {
    std::string rule;   // "E-PKG-12"
    std::string what;   // short file-name part
    std::string zip;
};

inline std::vector<Fixture> invalid_package_fixtures() {
    using pbstore::json::Value;
    std::vector<Fixture> out;
    auto add = [&](const char* rule, const char* what, std::string zip) { out.push_back({rule, what, std::move(zip)}); };
    const SampleApp base = make_sample_app();

    // E-PKG-01: not a readable ZIP, encrypted entry, unsupported compression
    add("E-PKG-01", "garbage", pseudo_bytes(300, 7));
    add("E-PKG-01", "truncated", build_package(base).substr(0, 200));
    { std::vector<ZEntry> f = base.files; f[1].flags = 1; add("E-PKG-01", "encrypted", build_package(base.manifest, f)); }
    { std::vector<ZEntry> f = base.files; f[1].method = 12; add("E-PKG-01", "bzip2", build_package(base.manifest, f)); }

    // E-PKG-02: a limit is exceeded
    { std::vector<ZEntry> many; for (int i = 0; i < 1001; ++i) many.push_back(file("applications/f" + std::to_string(i) + ".txt", "x"));
      std::vector<ZEntry> all; all.push_back(file("manifest.json", pbstore::json::write(base.manifest))); for (auto& e : many) all.push_back(e);
      add("E-PKG-02", "too-many-entries", build_zip(all)); }
    { std::vector<ZEntry> f = base.files; f.push_back(file("applications/zeros.bin", std::string(5 * 1024 * 1024, '\0')));
      add("E-PKG-02", "compression-ratio", build_package(base.manifest, f)); }
    { std::vector<ZEntry> f = base.files; ZEntry big = file("applications/big.bin", "x"); big.declared_usize = 150u * 1024 * 1024;
      ZEntry big2 = file("applications/big2.bin", "x"); big2.declared_usize = 150u * 1024 * 1024;
      add("E-PKG-02", "declared-size", build_package(base.manifest, f, {big, big2})); }

    // E-PKG-03: unsafe, duplicate or case-colliding names, symbolic links
    { std::vector<ZEntry> f = base.files; f.push_back(file("applications/../escape.txt", "x")); add("E-PKG-03", "dotdot", build_package(base.manifest, f)); }
    { std::vector<ZEntry> f = base.files; f.push_back(file("/absolute.txt", "x")); add("E-PKG-03", "absolute", build_package(base.manifest, f)); }
    { std::vector<ZEntry> f = base.files; f.push_back(file("applications\\win.txt", "x")); add("E-PKG-03", "backslash", build_package(base.manifest, f)); }
    { std::vector<ZEntry> f = base.files; f.push_back(file("applications/.hidden", "x")); add("E-PKG-03", "dot-segment", build_package(base.manifest, f)); }
    { std::vector<ZEntry> f = base.files; f.push_back(file("applications/Readme.txt", "a")); f.push_back(file("applications/README.txt", "b")); add("E-PKG-03", "case-collision", build_package(base.manifest, f)); }
    { std::vector<ZEntry> f = base.files; f.push_back(symlink("applications/link", "/etc/passwd")); add("E-PKG-03", "symlink", build_package(base.manifest, f)); }

    // E-PKG-04: something other than manifest.json and applications/ at the root
    { std::vector<ZEntry> f = base.files; add("E-PKG-04", "extra-root-file", build_package(base.manifest, f, {file("INSTALL.md", "# Install")})); }

    // E-PKG-05: manifest.json missing, too large, not JSON
    { std::vector<ZEntry> all = base.files; add("E-PKG-05", "missing-manifest", build_zip(all)); }
    { std::vector<ZEntry> all = {file("manifest.json", "{ not json")}; for (auto& e : base.files) all.push_back(e); add("E-PKG-05", "bad-json", build_zip(all)); }
    { std::vector<ZEntry> all = {file("manifest.json", pbstore::json::write(base.manifest) + std::string(300 * 1024, ' '))}; for (auto& e : base.files) all.push_back(e); add("E-PKG-05", "manifest-too-large", build_zip(all)); }

    // E-PKG-06: schema version
    { Value m = base.manifest; set_member(m, "schema_version", Value::from_string("2.0")); add("E-PKG-06", "newer-major", build_package(m, base.files)); }
    { Value m = base.manifest; set_member(m, "schema_version", Value::from_string("one")); add("E-PKG-06", "malformed", build_package(m, base.files)); }

    // E-PKG-07: required field missing or wrong type
    { Value m = base.manifest; remove_member(m, "menu_entry"); add("E-PKG-07", "no-menu-entry", build_package(m, base.files)); }
    { Value m = base.manifest; set_member(m, "name", Value::from_int(5)); add("E-PKG-07", "name-not-a-string", build_package(m, base.files)); }
    { Value m = base.manifest; set_member(m, "files", Value::make_array()); add("E-PKG-07", "empty-files", build_package(m, base.files)); }
    { Value m = base.manifest; remove_member(m, "data_dir"); add("E-PKG-07", "no-data-dir", build_package(m, base.files)); }

    // E-PKG-08: id, name, version, firmware, device formats
    { Value m = base.manifest; set_member(m, "id", Value::from_string("Bad_Id")); add("E-PKG-08", "bad-id", build_package(m, base.files)); }
    { Value m = base.manifest; set_member(m, "version", Value::from_string("1.2")); add("E-PKG-08", "bad-version", build_package(m, base.files)); }
    { Value m = base.manifest; set_member(m, "min_firmware", Value::from_string("6")); add("E-PKG-08", "bad-firmware", build_package(m, base.files)); }
    { Value m = base.manifest; set_member(m, "target_device", Value::from_string("Other Reader")); add("E-PKG-08", "bad-device", build_package(m, base.files)); }

    // E-PKG-09: files entries malformed or duplicated
    { Value m = base.manifest; Value& list = member(m, "files"); list.array[0].set("path", Value::from_string("system/outside.bin")); add("E-PKG-09", "outside-applications", build_package(m, base.files)); }
    { Value m = base.manifest; Value& list = member(m, "files"); list.array.push_back(list.array[0]); add("E-PKG-09", "duplicate-path", build_package(m, base.files)); }
    { Value m = base.manifest; Value& list = member(m, "files"); list.array[0].set("sha256", Value::from_string("NOT-A-HASH")); add("E-PKG-09", "bad-sha-format", build_package(m, base.files)); }

    // E-PKG-10: a listed file is missing from the ZIP
    { std::vector<ZEntry> f = base.files; f.erase(f.begin() + 1); add("E-PKG-10", "missing-file", build_package(base.manifest, f)); }

    // E-PKG-11: a file under applications/ is not listed
    { std::vector<ZEntry> f = base.files; f.push_back(file("applications/unlisted.txt", "extra")); add("E-PKG-11", "unlisted-file", build_package(base.manifest, f)); }

    // E-PKG-12: SHA-256 or size differs from the manifest
    { std::vector<ZEntry> f = base.files; f[1].data[10] = static_cast<char>(f[1].data[10] ^ 1); add("E-PKG-12", "sha-mismatch", build_package(base.manifest, f)); }
    { Value m = base.manifest; Value& list = member(m, "files"); list.array[0].set("size", Value::from_int(1)); add("E-PKG-12", "size-mismatch", build_package(m, base.files)); }

    // E-PKG-13: menu path or icons not among the listed files
    { Value m = base.manifest; member(m, "menu_entry").set("path", Value::from_string("applications/nothere.app")); add("E-PKG-13", "menu-path-not-listed", build_package(m, base.files)); }
    { Value m = base.manifest; member(m, "menu_entry").set("icon", Value::from_string("applications/icons/nothere.bmp")); add("E-PKG-13", "icon-not-listed", build_package(m, base.files)); }
    { Value m = base.manifest; member(m, "menu_entry").set("focused_icon", Value::from_string("applications/icons/nothere_f.bmp")); add("E-PKG-13", "focused-icon-not-listed", build_package(m, base.files)); }

    // E-PKG-14: menu entry id, title or group malformed
    { Value m = base.manifest; member(m, "menu_entry").set("id", Value::from_string("SampleApp")); add("E-PKG-14", "menu-id-without-prefix", build_package(m, base.files)); }
    { Value m = base.manifest; member(m, "menu_entry").set("title", Value::from_string("")); add("E-PKG-14", "empty-title", build_package(m, base.files)); }
    { Value m = base.manifest; member(m, "menu_entry").set("group", Value::from_string("General")); add("E-PKG-14", "group-without-at", build_package(m, base.files)); }

    // E-PKG-15: data_dir malformed or reserved
    { Value m = base.manifest; set_member(m, "data_dir", Value::from_string("system/config")); add("E-PKG-15", "reserved-data-dir", build_package(m, base.files)); }
    { Value m = base.manifest; set_member(m, "data_dir", Value::from_string("data")); add("E-PKG-15", "malformed-data-dir", build_package(m, base.files)); }
    { Value m = base.manifest; set_member(m, "data_dir", Value::from_string("applications/mine")); add("E-PKG-15", "data-dir-inside-applications", build_package(m, base.files)); }
    return out;
}

}  // namespace pbtest
