// T039: pack and check-package through the command line (docs/contracts/cli.md).
#include "pbstore/json.h"
#include "pbstore/package_check.h"
#include "pbstore/zip_package.h"
#include "run_tool.h"
#include "sample_app.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>
#include <unistd.h>

using namespace pbstore;
using json::Value;

// Lays out a package directory: manifest.json (the template) and the applications/ tree.
static void lay_out(const pbtest::TmpDir& t, const pbtest::SampleApp& app, Value manifest_template) {
    for (const auto& f : app.files) t.write_p("pkg/" + f.name, f.data);
    t.write("pkg/manifest.json", json::write(manifest_template));
}
static Value template_without_files(const pbtest::SampleApp& app) {
    Value m = app.manifest;
    pbtest::remove_member(m, "files");
    return m;
}
static bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

PB_TEST(pack_computes_the_file_list_and_writes_a_conforming_package) {
    pbtest::TmpDir t;
    ::mkdir(t.file("pkg").c_str(), 0755);
    auto app = pbtest::make_sample_app();
    lay_out(t, app, template_without_files(app));
    auto r = pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("app.zip")});
    PB_CHECK_EQ(r.code, 0);
    const std::string zip = t.read("app.zip");
    PB_CHECK(zip.size() > 100);
    PB_CHECK(has(r.out, sha256_hex(zip).c_str()));                       // prints the package hash
    PB_CHECK(has(r.out, std::to_string(zip.size()).c_str()));            // and size
    PackageCheckResult c = check_package(zip);
    PB_CHECK(c.ok());
    PB_CHECK_EQ(c.manifest->files.size(), size_t(4));
    for (size_t i = 1; i < c.manifest->files.size(); ++i) PB_CHECK(c.manifest->files[i - 1].path < c.manifest->files[i].path);
    // the file list equals what the helper computes by hand
    Value expected = pbtest::files_value(app.files);
    for (size_t i = 0; i < c.manifest->files.size(); ++i) {
        PB_CHECK_EQ(c.manifest->files[i].sha256, *expected.array[i].get_string("sha256"));
        PB_CHECK_EQ(c.manifest->files[i].size, *expected.array[i].get_int("size"));
    }
}

PB_TEST(pack_replaces_a_stale_files_list_and_keeps_other_fields) {
    pbtest::TmpDir t;
    ::mkdir(t.file("pkg").c_str(), 0755);
    auto app = pbtest::make_sample_app();
    Value m = app.manifest;                                           // has a correct list; make it stale
    pbtest::member(m, "files").array[0].set("sha256", Value::from_string(std::string(64, '0')));
    pbtest::member(m, "files").array.push_back(pbtest::member(m, "files").array[0]);
    m.set("future_field", Value::from_string("kept"));
    lay_out(t, app, m);
    auto r = pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("app.zip")});
    PB_CHECK_EQ(r.code, 0);
    ZipArchive z;
    PB_CHECK(z.open(t.read("app.zip"), ZipLimits()).empty());
    std::string manifest_text, error;
    for (const ZipEntry& e : z.entries()) if (e.name == "manifest.json") PB_CHECK(z.read(e, 1 << 20, &manifest_text, &error));
    auto doc = json::parse(manifest_text);
    PB_CHECK(doc.has_value());
    PB_CHECK_EQ(doc->get_string("future_field").value_or(""), std::string("kept"));
    PB_CHECK(check_package(t.read("app.zip")).ok());
}

PB_TEST(pack_is_deterministic) {
    pbtest::TmpDir t;
    ::mkdir(t.file("pkg").c_str(), 0755);
    auto app = pbtest::make_sample_app();
    lay_out(t, app, template_without_files(app));
    PB_CHECK_EQ(pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("a.zip")}).code, 0);
    ::sleep(1);   // timestamps must not leak into the package
    PB_CHECK_EQ(pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("b.zip")}).code, 0);
    PB_CHECK_EQ(t.read("a.zip"), t.read("b.zip"));
    PB_CHECK_EQ(sha256_hex(t.read("a.zip")), sha256_hex(t.read("b.zip")));
}

PB_TEST(pack_rejects_an_invalid_tree_and_writes_no_package) {
    {   // illegal file name
        pbtest::TmpDir t; ::mkdir(t.file("pkg").c_str(), 0755);
        auto app = pbtest::make_sample_app(); lay_out(t, app, template_without_files(app));
        t.write_p("pkg/applications/bad@name.txt", "x");
        auto r = pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("app.zip")});
        PB_CHECK_EQ(r.code, 1);
        PB_CHECK(has(r.err, "error[E-PKG-03]") && has(r.err, "bad@name.txt"));
        PB_CHECK(!t.exists("app.zip"));
    }
    {   // symbolic link
        pbtest::TmpDir t; ::mkdir(t.file("pkg").c_str(), 0755);
        auto app = pbtest::make_sample_app(); lay_out(t, app, template_without_files(app));
        PB_CHECK(::symlink("/etc/hostname", t.file("pkg/applications/link").c_str()) == 0);
        auto r = pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("app.zip")});
        PB_CHECK_EQ(r.code, 1);
        PB_CHECK(has(r.err, "E-PKG-03") && has(r.err, "link"));
        PB_CHECK(!t.exists("app.zip"));
    }
    {   // the menu file does not exist in the tree
        pbtest::TmpDir t; ::mkdir(t.file("pkg").c_str(), 0755);
        auto app = pbtest::make_sample_app();
        Value m = template_without_files(app);
        pbtest::member(m, "menu_entry").set("path", Value::from_string("applications/missing.app"));
        lay_out(t, app, m);
        auto r = pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("app.zip")});
        PB_CHECK_EQ(r.code, 1);
        PB_CHECK(has(r.err, "error[E-PKG-13]"));
        PB_CHECK(!t.exists("app.zip"));
    }
    {   // template that is not JSON
        pbtest::TmpDir t; ::mkdir(t.file("pkg").c_str(), 0755);
        auto app = pbtest::make_sample_app(); lay_out(t, app, template_without_files(app));
        t.write("pkg/manifest.json", "{ nope");
        auto r = pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("app.zip")});
        PB_CHECK_EQ(r.code, 1);
        PB_CHECK(has(r.err, "E-PKG-05"));
    }
}

PB_TEST(pack_usage_errors_exit_2) {
    pbtest::TmpDir t;
    PB_CHECK_EQ(pbtest::run_tool({"pack"}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"pack", t.file("pkg")}).code, 2);                               // no --out
    PB_CHECK_EQ(pbtest::run_tool({"pack", t.file("nothere"), "--out", t.file("x.zip")}).code, 2);   // no such directory
    ::mkdir(t.file("empty").c_str(), 0755);
    PB_CHECK_EQ(pbtest::run_tool({"pack", t.file("empty"), "--out", t.file("x.zip")}).code, 2);     // no manifest.json
    PB_CHECK(!t.exists("x.zip"));
}

PB_TEST(check_package_exit_codes_and_output) {
    pbtest::TmpDir t;
    auto app = pbtest::make_sample_app();
    const std::string zip = pbtest::build_package(app);
    t.write("good.zip", zip);
    auto good = pbtest::run_tool({"check-package", t.file("good.zip")});
    PB_CHECK_EQ(good.code, 0);
    PB_CHECK(has(good.out, sha256_hex(zip).c_str()) && has(good.out, std::to_string(zip.size()).c_str()));
    std::vector<pbtest::ZEntry> f = app.files;
    f[1].data[2] = static_cast<char>(f[1].data[2] ^ 1);
    t.write("bad.zip", pbtest::build_package(app.manifest, f));
    auto bad = pbtest::run_tool({"check-package", t.file("bad.zip"), "--github"});
    PB_CHECK_EQ(bad.code, 1);
    PB_CHECK(has(bad.err, "error[E-PKG-12] applications/sample-app.bin"));
    PB_CHECK(has(bad.out, "::error file="));
    PB_CHECK_EQ(pbtest::run_tool({"check-package", t.file("missing.zip")}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"check-package"}).code, 2);
}

PB_TEST_MAIN()
