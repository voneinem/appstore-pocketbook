// T082 (User Story 7): a package laid out like the existing sibling releases fails with readable
// rules, and the same tree passes once it follows the contract.
#include "pbstore/json.h"
#include "pbstore/package_check.h"
#include "run_tool.h"
#include "sample_app.h"
#include "test_support.h"
#include "tmp_dir.h"
#include "zip_builder.h"

#include <sys/stat.h>

using namespace pbstore;
using json::Value;
using pbtest::file;

static bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

// the layout of a current release (PB Notes): INSTALL.md at the root, directory entries, a licenses tree
static std::vector<pbtest::ZEntry> sibling_layout(bool with_install_md) {
    std::vector<pbtest::ZEntry> z;
    if (with_install_md) z.push_back(file("INSTALL.md", "# Installing\nCopy applications/ to the reader.\n"));
    z.push_back(pbtest::directory("applications/"));
    z.push_back(pbtest::directory("applications/icons/"));
    z.push_back(pbtest::directory("applications/myapp-licenses/"));
    z.push_back(pbtest::directory("applications/myapp-licenses/licenses/"));
    z.push_back(file("applications/myapp.app", "#!/bin/sh\nexec ./myapp.bin\n"));
    z.push_back(file("applications/myapp.bin", pbtest::pseudo_bytes(3000, 1)));
    z.push_back(file("applications/myapp-cacert.pem", pbtest::pseudo_bytes(800, 2)));
    z.push_back(file("applications/icons/myapp.bmp", pbtest::pseudo_bytes(500, 3)));
    z.push_back(file("applications/icons/myapp_f.bmp", pbtest::pseudo_bytes(500, 4)));
    z.push_back(file("applications/myapp-licenses/LICENSE", "MIT License ..."));
    z.push_back(file("applications/myapp-licenses/licenses/curl.txt", "curl license ..."));
    return z;
}

static Value manifest_for(const std::vector<pbtest::ZEntry>& zip_entries) {
    std::vector<pbtest::ZEntry> app_files;
    for (const auto& e : zip_entries)
        if (e.name.rfind("applications/", 0) == 0) app_files.push_back(e);
    Value m = pbtest::make_sample_app("myapp", "1.0.0", "U_MyApp", "system/myapp").manifest;
    pbtest::set_member(m, "files", pbtest::files_value(app_files));
    return m;
}

PB_TEST(a_release_laid_out_like_the_current_ones_fails_with_readable_rules) {
    // no manifest.json and an INSTALL.md at the root
    PackageCheckResult r = check_package(pbtest::build_zip(sibling_layout(true)));
    PB_CHECK(!r.ok());
    bool e04 = false, e05 = false;
    for (const Finding& f : r.findings) {
        if (f.rule_id == "E-PKG-04" && f.message.find("INSTALL.md") != std::string::npos) e04 = true;
        if (f.rule_id == "E-PKG-05" && f.message.find("manifest.json is missing") != std::string::npos) e05 = true;
    }
    PB_CHECK(e04 && e05);
    PB_CHECK_EQ(r.findings.size(), size_t(2));                      // nothing else is wrong with such a package
    // a manifest is added but INSTALL.md remains: the root rule names the file
    std::vector<pbtest::ZEntry> with_manifest = sibling_layout(true);
    with_manifest.push_back(file("manifest.json", json::write(manifest_for(with_manifest))));
    PackageCheckResult r2 = check_package(pbtest::build_zip(with_manifest));
    PB_CHECK(!r2.ok());
    PB_CHECK_EQ(r2.findings.size(), size_t(1));
    PB_CHECK_EQ(r2.findings[0].rule_id, std::string("E-PKG-04"));
    PB_CHECK(r2.findings[0].message.find("INSTALL.md") != std::string::npos);
}

PB_TEST(the_same_tree_passes_after_pack_without_install_md) {
    pbtest::TmpDir t;
    ::mkdir(t.file("pkg").c_str(), 0755);
    for (const auto& e : sibling_layout(false))
        if (e.name.back() != '/') t.write_p("pkg/" + e.name, e.data);
    Value tmpl = manifest_for(sibling_layout(false));
    pbtest::remove_member(tmpl, "files");
    t.write("pkg/manifest.json", json::write(tmpl));
    auto r = pbtest::run_tool({"pack", t.file("pkg"), "--out", t.file("myapp-1.0.0.zip")});
    PB_CHECK_EQ(r.code, 0);
    auto c = pbtest::run_tool({"check-package", t.file("myapp-1.0.0.zip")});
    PB_CHECK_EQ(c.code, 0);
    // the unchanged release of the same app still fails: INSTALL.md at the root
    t.write("old.zip", pbtest::build_zip(sibling_layout(true)));
    auto old = pbtest::run_tool({"check-package", t.file("old.zip")});
    PB_CHECK_EQ(old.code, 1);
    PB_CHECK(has(old.err, "error[E-PKG-04] INSTALL.md"));
    PB_CHECK(has(old.err, "error[E-PKG-05] manifest.json"));
}

PB_TEST_MAIN()
