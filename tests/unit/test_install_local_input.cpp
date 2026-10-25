// T100: a package file chosen on this computer.
#include "install_rig.h"
#include "package_fixtures.h"
#include "pbstore/install/local_input.h"
#include "pbstore/install/posix_local_file.h"
#include "test_support.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
LocalPackageResult read(const std::string& path) { return read_local_package(path, read_regular_file); }

}  // namespace

PB_TEST(a_regular_readable_package_is_read_and_checked) {
    TmpDir d;
    SampleApp app = nested_sample_app();
    d.write("testapp-1.2.0.zip", build_package(app));
    LocalPackageResult r = read(d.file("testapp-1.2.0.zip"));
    PB_CHECK(r.findings.empty());
    PB_CHECK(r.package.manifest.has_value());
    PB_CHECK_EQ(r.package.source, std::string("local"));
    PB_CHECK_EQ(r.package.manifest->id, std::string("testapp"));
}

PB_TEST(a_folder_a_pipe_a_device_a_link_a_missing_file_and_a_too_large_file_give_e_ins_27) {
    TmpDir d;
    d.write("real.zip", build_package(nested_sample_app()));
    ::mkdir(d.file("folder.zip").c_str(), 0755);
    PB_CHECK_EQ(::mkfifo(d.file("pipe.zip").c_str(), 0600), 0);
    PB_CHECK_EQ(::symlink(d.file("real.zip").c_str(), d.file("link.zip").c_str()), 0);
    {
        int fd = ::open(d.file("huge.zip").c_str(), O_CREAT | O_WRONLY, 0600);
        PB_CHECK(fd >= 0);
        PB_CHECK_EQ(::ftruncate(fd, 64ll * 1024 * 1024 + 1), 0);       // sparse: one byte over the limit
        ::close(fd);
    }
    for (const std::string& path : {d.file("folder.zip"), d.file("pipe.zip"), std::string("/dev/null"), d.file("link.zip"), d.file("missing.zip"), d.file("huge.zip")}) {
        LocalPackageResult r = read(path);
        if (!has_rule(r.findings, "E-INS-27")) PB_FAIL_AT("no E-INS-27 for " + path);
        PB_CHECK(!r.package.manifest.has_value());
        PB_CHECK(has(r.findings[0].message, path));
    }
}

PB_TEST(the_bytes_are_read_once_and_a_later_change_of_the_file_does_not_matter) {
    TmpDir d;
    SampleApp app = nested_sample_app();
    d.write("p.zip", build_package(app));
    LocalPackageResult r = read(d.file("p.zip"));
    PB_CHECK(r.package.manifest.has_value());
    d.write("p.zip", "something else entirely");
    ZipContent content(r.package.bytes);
    std::string got;
    PB_CHECK(content.read("applications/testapp.app", &got));
    PB_CHECK_EQ(got, app.files[0].data);
}

PB_TEST(every_package_rule_applies_and_the_entry_checks_are_replaced_by_the_marking) {
    int checked = 0;
    for (const Fixture& f : invalid_package_fixtures()) {
        Package p = verify_local_package(f.zip);
        if (p.ok()) PB_FAIL_AT(f.rule + "/" + f.what + ": was accepted");
        if (p.findings.empty() || p.findings[0].rule_id != f.rule) PB_FAIL_AT(f.rule + "/" + f.what + ": first finding is " + (p.findings.empty() ? std::string("none") : p.findings[0].rule_id));
        for (const auto& finding : p.findings) PB_CHECK(finding.rule_id.compare(0, 5, "E-ENT") != 0);
        ++checked;
    }
    PB_CHECK(checked > 30);
    PB_CHECK(has(std::string(kLocalMarking), "not signed by the maintainer"));
    PB_CHECK(has(std::string(kLocalMarking), "not from the catalog"));
    // a good package passes without any catalog entry
    SampleApp app = nested_sample_app();
    PB_CHECK(verify_local_package(build_package(app)).ok());
}

PB_TEST(a_local_package_is_checked_against_installed_apps_and_different_catalog_apps) {
    // the engine does the comparison; here a local package whose menu entry belongs to an installed app
    FakeReader reader = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_TestApp"));
    EngineRig rig(reader);
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.host.add_app(nested_sample_app("otherapp", "1.0.0", "U_TestApp"));
    rig.host.publish(100);
    rig.write_reader();
    TmpDir d;
    d.write("testapp.zip", build_package(nested_sample_app()));
    Engine& engine = rig.make_engine();
    auto handle = engine.open_reader(rig.root());
    CancelToken cancel;
    Action action;
    action.type = Action::Type::InstallLocal;
    action.local_file = d.file("testapp.zip");
    auto plan = engine.plan(**handle.value, action, nullptr, cancel);
    PB_CHECK(!plan.ok());
    PB_CHECK(has_rule(plan.findings, "E-INS-15"));
    // a catalog app (not installed) that uses the same data folder as the local package
    EngineRig rig2;
    if (!rig2.ready()) PB_SKIP("openssl CLI not available");
    SampleApp other = nested_sample_app("otherapp", "1.0.0", "U_OtherApp");
    other.manifest.set("data_dir", pbstore::json::Value::from_string("system/testapp"));
    rig2.host.add_app(other);
    rig2.host.publish(100);
    rig2.write_reader();
    Engine& engine2 = rig2.make_engine();
    auto handle2 = engine2.open_reader(rig2.root());
    auto plan2 = engine2.plan(**handle2.value, action, nullptr, cancel);
    PB_CHECK(!plan2.ok());
    PB_CHECK(has_rule(plan2.findings, "E-INS-16"));
}

PB_TEST(without_the_catalog_the_install_goes_on_offline_and_says_so_and_the_serial_is_untouched) {
    EngineRig rig;
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.host.publish(100);
    rig.write_reader();
    TmpDir d;
    d.write("testapp.zip", build_package(nested_sample_app()));
    // the downloader cannot reach anything
    Downloader dead = [](const std::string&, uint64_t, long, const DownloadProgress&) {
        FetchResult r;
        r.kind = FetchKind::Unreachable;
        r.error = "cannot connect";
        return r;
    };
    Engine& engine = rig.make_engine(dead);
    auto handle = engine.open_reader(rig.root());
    CancelToken cancel;
    Action action;
    action.type = Action::Type::InstallLocal;
    action.local_file = d.file("testapp.zip");
    auto plan = engine.plan(**handle.value, action, nullptr, cancel);
    PB_CHECK(plan.ok());
    bool said = false;
    for (const auto& n : plan.notices) said = said || has(n.message, "offline");
    PB_CHECK(said);
    PB_CHECK_EQ(plan.value->plan.source, std::string("local"));
    PB_CHECK(has(plan.value->plan.source_label, "not signed by the maintainer"));
    StateStore store(rig.state);
    PB_CHECK(!store.last_serial().has_value());
}

PB_TEST_MAIN()
