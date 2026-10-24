// T065 (SC-011): rehearsal of the key rotation of docs/maintainer-keys.md with the real commands.
#include "pbstore/catalog.h"
#include "pbstore/json.h"
#include "entry_builder.h"
#include "run_tool.h"
#include "sample_app.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>

using namespace pbstore;

static bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

PB_TEST(a_rotation_never_breaks_a_consumer_that_trusts_both_keys) {
    pbtest::TmpDir t;
    for (const char* d : {"entries", "packages", "keys_both", "keys_old", "keys_new", "out1", "out2"}) ::mkdir(t.file(d).c_str(), 0755);
    t.write("entries/alpha-app.json", json::write(pbtest::sample_entry_doc("alpha-app", "1.2.0")));
    t.write("packages/alpha-app-inkpad-one-1.2.0.zip", pbtest::build_package(pbtest::make_sample_app("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app", 3)));
    // the maintainer creates the current and the next key
    PB_CHECK_EQ(pbtest::run_tool({"keygen", "--private-out", t.file("current.key"), "--public-out", t.file("keys_old/current.pub")}).code, 0);
    PB_CHECK_EQ(pbtest::run_tool({"keygen", "--private-out", t.file("next.key"), "--public-out", t.file("keys_new/next.pub")}).code, 0);
    t.write("keys_both/current.pub", t.read("keys_old/current.pub"));
    t.write("keys_both/next.pub", t.read("keys_new/next.pub"));

    auto build_sign = [&](const char* out, const char* key_file, int64_t now) {
        auto b = pbtest::run_tool({"build", "--entries", t.file("entries"), "--out", t.file(out), "--package-dir", t.file("packages"), "--first-publish", "--now", std::to_string(now)});
        PB_CHECK_EQ(b.code, 0);
        auto s = pbtest::run_tool({"sign", "--catalog", t.file(std::string(out) + "/catalog.json")}, {"PBSTORE_SIGNING_KEY=" + t.read(key_file)});
        PB_CHECK_EQ(s.code, 0);
    };
    auto verify = [&](const char* out, const char* keys) {
        return pbtest::run_tool({"verify", "--catalog", t.file(std::string(out) + "/catalog.json"), "--sig", t.file(std::string(out) + "/catalog.json.sig"), "--keys", t.file(keys)});
    };

    // 1. stores that trust both keys are in use; the catalog is still signed with the current key
    build_sign("out1", "current.key", 1000);
    PB_CHECK_EQ(verify("out1", "keys_both").code, 0);
    PB_CHECK_EQ(verify("out1", "keys_old").code, 0);
    // 2. the signing secret is switched to the next key and the next catalog is published
    build_sign("out2", "next.key", 2000);
    PB_CHECK_EQ(verify("out2", "keys_both").code, 0);                    // consumers that trust both: no failed verification
    auto old_only = verify("out2", "keys_old");                          // a store that was never updated rejects, fail closed
    PB_CHECK_EQ(old_only.code, 1);
    PB_CHECK(has(old_only.err, "error[E-CAT-01]"));
    // 3. later the retired key is dropped from the list: the new catalog verifies, the old one no longer does
    PB_CHECK_EQ(verify("out2", "keys_new").code, 0);
    PB_CHECK_EQ(verify("out1", "keys_new").code, 1);
    // the rollback rule still works across the rotation
    auto rollback = pbtest::run_tool({"verify", "--catalog", t.file("out1/catalog.json"), "--sig", t.file("out1/catalog.json.sig"), "--keys", t.file("keys_both"), "--last-serial", "2000"});
    PB_CHECK_EQ(rollback.code, 1);
    PB_CHECK(has(rollback.err, "E-CAT-04"));
}

PB_TEST_MAIN()
