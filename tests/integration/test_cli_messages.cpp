// T109 (spec SC-009): every failure of the spec's edge cases gives a message with the rule id, what happened,
// whether the reader changed (unchanged / restored to the previous state / repair needed) and what to do next,
// and the exit status of the table in contracts/rules.md.
#include "install_rig.h"
#include "package_fixtures.h"
#include "pbstore/install/messages.h"
#include "test_support.h"

#include <sys/stat.h>
#include <unistd.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
const std::vector<std::string> kConf = {"--confirm-plan", "--confirm-unknown-model", "--confirm-unknown-firmware"};

std::vector<std::string> concat(std::vector<std::string> a, const std::vector<std::string>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

struct Rig : CliRig {
    explicit Rig(const FakeReader& r = FakeReader()) : CliRig(r) {}
    void start(const std::function<void()>& catalog = nullptr) {
        if (!host.ready()) return;
        host.add_app(nested_sample_app());
        if (catalog) catalog();
        host.publish(100);
        ::mkdir((dir.path() + "/home").c_str(), 0755);
        write_reader();
    }
};

// checks one failure
void expect_message(const std::string& what, const RunResult& res, const char* id, int status, const char* effect = "not changed") {
    if (res.code != status) PB_FAIL_AT(what + ": exit status " + std::to_string(res.code) + ", expected " + std::to_string(status) + "\n" + res.err);
    if (!has(res.err, std::string("error[") + id + "]")) PB_FAIL_AT(what + ": no error[" + id + "] in\n" + res.err);
    if (!has(res.err, effect)) PB_FAIL_AT(what + ": does not say \"" + effect + "\" in\n" + res.err);
    if (!has(res.err, "Next:")) PB_FAIL_AT(what + ": names no next step in\n" + res.err);
    // what happened: a sentence after the id, not only a code
    const size_t at = res.err.find(std::string("error[") + id + "]");
    const size_t eol = res.err.find('\n', at);
    if (eol - at < (std::string("error[") + id + "] x: x").size() + 10) PB_FAIL_AT(what + ": the message says too little: " + res.err.substr(at, eol - at));
    for (const char* trace : {"terminate called", "Segmentation", "std::", "stack trace", "backtrace", "core dumped"})
        if (has(res.err + res.out, trace)) PB_FAIL_AT(what + ": the output looks like a crash: " + trace);
    for (const char* bad : {"sudo", "administrator", "chmod"})
        if (has(res.err + res.out, bad)) PB_FAIL_AT(what + ": the output mentions " + bad);
}

}  // namespace

PB_TEST(no_reader_several_readers_and_the_mtp_hint) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.start();
    expect_message("no reader", r.run(concat({"install", "testapp"}, kConf), {"PBSTORE_TEST_MOUNTS="}), "E-INS-01", 2);
    FakeReader second;
    second.write_to(r.dir.path() + "/second");
    expect_message("several readers",
                   r.run(concat({"install", "testapp"}, kConf), {"PBSTORE_TEST_MOUNTS=" + r.root() + "|vfat|1;" + r.dir.path() + "/second|vfat|1"}), "E-INS-02", 2);
    RunResult mtp = r.run(concat({"install", "testapp"}, kConf), {"PBSTORE_TEST_MOUNTS=", "PBSTORE_TEST_MTP=/run/user/1000/gvfs/mtp:host=PocketBook"});
    expect_message("mtp", mtp, "E-INS-04", 2);
    PB_CHECK(has(mtp.err, "MTP"));
    expect_message("charge only", r.run(concat({"install", "testapp"}, kConf), {"PBSTORE_TEST_MOUNTS=", "PBSTORE_TEST_USB=PocketBook"}), "E-INS-04", 2);
}

PB_TEST(not_a_reader_not_a_pocketbook_and_the_firmware_cases) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.start();
    ::mkdir((r.dir.path() + "/empty").c_str(), 0755);
    expect_message("not a reader folder", r.run(concat({"install", "testapp", "--reader", r.dir.path() + "/empty"}, kConf)), "E-INS-03", 2);
    expect_message("not an InkPad One", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf), {"PBSTORE_TEST_MODEL=1040", "PBSTORE_TEST_FIRMWARE=6.11.1437"}), "E-INS-08", 1);
    expect_message("firmware too low", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf), {"PBSTORE_TEST_MODEL=1030", "PBSTORE_TEST_FIRMWARE=6.1.0"}), "E-INS-10", 1);
    expect_message("firmware unknown", r.run({"install", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-model"}, {"PBSTORE_TEST_MODEL=1030"}), "E-INS-11", 2);
    expect_message("model unknown", r.run({"install", "testapp", "--reader", r.root(), "--confirm-plan", "--confirm-unknown-firmware"}, {"PBSTORE_TEST_FIRMWARE=6.11.1437"}), "E-INS-09", 2);
}

PB_TEST(read_only_full_storage_and_no_write_rights) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.start();
    RunResult ro = r.run(concat({"install", "testapp", "--reader", r.root()}, kConf), {"PBSTORE_TEST_READ_ONLY=1"});
    expect_message("read-only", ro, "E-INS-05", 2);
    PB_CHECK(has(ro.err, "read-only"));
    PB_CHECK(has(ro.err, "Mount") || has(ro.err, "mount"));
    RunResult full = r.run(concat({"install", "testapp", "--reader", r.root()}, kConf), {"PBSTORE_TEST_FREE_BYTES=1000000"});
    expect_message("full storage", full, "E-INS-07", 2);
    PB_CHECK(has(full.err, "MiB"));
    ::chmod((r.root() + "/applications").c_str(), 0555);
    RunResult norights = r.run(concat({"install", "testapp", "--reader", r.root()}, kConf));
    ::chmod((r.root() + "/applications").c_str(), 0755);
    if (::geteuid() != 0) {
        expect_message("no write rights", norights, "E-INS-06", 2);
        PB_CHECK(has(norights.err, "owns") || has(norights.err, "owner"));
        PB_CHECK(has(norights.err, "mounted"));
    }
}

PB_TEST(the_reader_is_unplugged_in_the_middle_of_an_install) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.start();
    int seen_repair = 0, seen_restored = 0;
    for (int n : {5, 12, 20, 28, 33, 38}) {
        Rig fresh;
        fresh.start();
        RunResult res = fresh.run(concat({"install", "testapp", "--reader", fresh.root()}, kConf), {"PBSTORE_TEST_VANISH_AFTER=" + std::to_string(n)});
        PB_CHECK(res.code == 2 || res.code == 4);
        PB_CHECK(has(res.err, "error[E-INS-37]") || has(res.err, "error[E-INS-23]"));
        PB_CHECK(has(res.err, "Next:"));
        if (has(res.err, "repair needed")) ++seen_repair;
        if (has(res.err, "restored") || has(res.err, "not changed")) ++seen_restored;
        // the next run (reader back) finds and repairs it
        RunResult next = fresh.run({"repair", "--reader", fresh.root(), "--confirm-repair"});
        PB_CHECK_EQ(next.code, 0);
    }
    PB_CHECK(seen_repair + seen_restored >= 6);
}

PB_TEST(an_interrupted_earlier_install) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.start();
    r.run(concat({"install", "testapp", "--reader", r.root()}, kConf), {"PBSTORE_TEST_STOP_AFTER=30"});
    expect_message("interrupted", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-INS-23", 4, "repair needed");
}

PB_TEST(no_network_bad_signature_rollback_and_a_newer_schema) {
    Rig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.start();
    expect_message("no network", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf), {"PBSTORE_TEST_CATALOG_URL=https://localhost:1/catalog.json"}), "E-INS-26", 2);
    // bad signature
    r.host.files["/catalog.json"] += " ";
    expect_message("bad signature", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-CAT-01", 1);
    // rollback: first accept serial 200, then offer serial 150
    r.host.publish(200);
    PB_CHECK_EQ(r.run({"list", "--reader", r.root()}).code, 0);
    r.host.publish(150);
    expect_message("rollback", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-CAT-04", 1);
    // newer major schema
    r.host.catalog.schema_version = "2.0";
    r.host.publish(300);
    expect_message("newer schema", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-CAT-03", 1);
}

PB_TEST(package_rule_failures_app_not_in_the_catalog_and_claimed_ids_and_folders) {
    {   // a failing package rule
        Rig r;
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        r.start([&] {
            const std::string bad = invalid_package_fixtures()[0].zip;
            CatalogEntry& e = r.host.catalog.apps.back();
            r.host.files[package_path_of(e)] = bad;
            e.package_size = static_cast<int64_t>(bad.size());
            e.package_sha256 = sha256_hex(bad);
        });
        expect_message("package rule", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-PKG-01", 1);
    }
    {   // a package that writes outside the allowed folders is refused by the package rules
        Rig r;
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        r.start([&] {
            SampleApp app = nested_sample_app();
            app.files.push_back(file("applications/../escape.txt", "x"));
            const std::string bad = build_package(app.manifest, app.files);
            CatalogEntry& e = r.host.catalog.apps.back();
            r.host.files[package_path_of(e)] = bad;
            e.package_size = static_cast<int64_t>(bad.size());
            e.package_sha256 = sha256_hex(bad);
        });
        expect_message("outside the allowed folders", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-PKG-03", 1);
    }
    {
        Rig r;
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        r.start();
        expect_message("not in the catalog", r.run(concat({"install", "missing-app", "--reader", r.root()}, kConf)), "E-INS-28", 1);
    }
    {   // two apps claiming one menu entry / one data folder
        FakeReader f = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_TestApp"));
        Rig r(f);
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        r.start();
        expect_message("claimed id", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-INS-15", 1);
    }
    {
        FakeReader f = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_OtherApp"));
        Rig r(f);
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        r.start([&] {
            SampleApp app = nested_sample_app();
            app.manifest.set("data_dir", pbstore::json::Value::from_string("system/otherapp"));
            r.host.add_app(app);
        });
        expect_message("claimed folder", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-INS-16", 1);
    }
}

PB_TEST(the_desktop_configuration_is_invalid_or_incomplete_or_lacks_the_menu_group) {
    {
        FakeReader f;
        f.files["system/config/desktop/view.json"] = "{ \"applications\": ";
        Rig r(f);
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        r.start();
        expect_message("view.json invalid", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-INS-12", 1);
    }
    {
        FakeReader f;
        f.files["system/config/desktop/view.json"] = "{ \"applications\": {} }";
        Rig r(f);
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        r.start();
        expect_message("view.json incomplete", r.run(concat({"install", "testapp", "--reader", r.root()}, kConf)), "E-INS-13", 1);
    }
    {
        Rig r;
        if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
        r.start([&] {
            SampleApp app = nested_sample_app();
            for (auto& kv : app.manifest.object) if (kv.first == "menu_entry") kv.second.set("group", pbstore::json::Value::from_string("@Nowhere"));
            r.host.add_app(app);
        });
        RunResult res = r.run(concat({"install", "testapp", "--reader", r.root()}, kConf));
        expect_message("group missing", res, "E-INS-14", 1);
        PB_CHECK(has(res.err, "@General"));
    }
}

PB_TEST(every_message_names_the_reader_state_in_one_of_the_three_ways) {
    // the table of the registry: each rule maps to exactly one of the three states
    for (const auto& rule : install_rule_registry()) {
        const Message m = describe(make_error(rule.id, "subject", "something happened"));
        const bool ok = m.reader_effect.find("not changed") != std::string::npos || m.reader_effect.find("restored to the previous state") != std::string::npos ||
                        m.reader_effect.find("repair needed") != std::string::npos;
        PB_CHECK(ok);
    }
}

PB_TEST_MAIN()
