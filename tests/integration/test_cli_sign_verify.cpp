// T029: keygen, sign and verify through the command line (docs/contracts/cli.md).
#include "pbstore/base64.h"
#include "pbstore/catalog.h"
#include "run_tool.h"
#include "sample_catalog.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>

using namespace pbstore;

static mode_t mode_of(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 ? (st.st_mode & 0777) : 0;
}

PB_TEST(keygen_writes_both_files_with_the_documented_permissions) {
    pbtest::TmpDir t;
    auto r = pbtest::run_tool({"keygen", "--private-out", t.file("current.key"), "--public-out", t.file("current.pub")});
    PB_CHECK_EQ(r.code, 0);
    PB_CHECK(t.exists("current.key") && t.exists("current.pub"));
    PB_CHECK_EQ(mode_of(t.file("current.key")), mode_t(0600));
    const std::string pub = t.read("current.pub"), priv = t.read("current.key");
    PB_CHECK(pub.size() == 45 && pub.back() == '\n');               // 44 base64 characters + newline
    PB_CHECK(base64_decode(pub).has_value() && base64_decode(pub)->size() == 32);
    PB_CHECK(base64_decode(priv).has_value() && base64_decode(priv)->size() == 32);
    PB_CHECK(r.out.find(pub.substr(0, 44)) != std::string::npos);    // the public key is printed
    PB_CHECK(r.out.find(priv.substr(0, 44)) == std::string::npos && r.err.find(priv.substr(0, 44)) == std::string::npos);
}

PB_TEST(keygen_refuses_to_overwrite_and_refuses_a_git_work_tree) {
    pbtest::TmpDir t;
    t.write("exists.key", "old");
    auto r = pbtest::run_tool({"keygen", "--private-out", t.file("exists.key"), "--public-out", t.file("n.pub")});
    PB_CHECK_EQ(r.code, 2);
    PB_CHECK_EQ(t.read("exists.key"), std::string("old"));
    PB_CHECK(!t.exists("n.pub"));
    t.write("exists.pub", "old");
    r = pbtest::run_tool({"keygen", "--private-out", t.file("n.key"), "--public-out", t.file("exists.pub")});
    PB_CHECK_EQ(r.code, 2);
    PB_CHECK(!t.exists("n.key"));
    // a private path inside a git work tree is refused
    ::mkdir(t.file("work").c_str(), 0755);
    ::mkdir(t.file("work/.git").c_str(), 0755);
    ::mkdir(t.file("work/sub").c_str(), 0755);
    r = pbtest::run_tool({"keygen", "--private-out", t.file("work/sub/a.key"), "--public-out", t.file("a.pub")});
    PB_CHECK_EQ(r.code, 2);
    PB_CHECK(r.err.find("git") != std::string::npos);
    PB_CHECK(!t.exists("work/sub/a.key") && !t.exists("a.pub"));
    // the public key may live in the work tree
    r = pbtest::run_tool({"keygen", "--private-out", t.file("outside.key"), "--public-out", t.file("work/sub/a.pub")});
    PB_CHECK_EQ(r.code, 0);
    // missing options
    PB_CHECK_EQ(pbtest::run_tool({"keygen"}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"keygen", "--private-out", t.file("x.key")}).code, 2);
}

static void write_catalog_file(const pbtest::TmpDir& t, int64_t serial) {
    t.write("catalog.json", write_catalog(pbtest::sample_catalog(serial)));
}

static void make_keys(const pbtest::TmpDir& t) {
    ::mkdir(t.file("keys").c_str(), 0755);
    auto r = pbtest::run_tool({"keygen", "--private-out", t.file("signing.key"), "--public-out", t.file("keys/current.pub")});
    if (r.code != 0) throw pbtest::Failure{"keygen failed: " + r.err};
}

PB_TEST(sign_reads_the_environment_or_a_file_and_never_prints_the_key) {
    pbtest::TmpDir t;
    make_keys(t);
    write_catalog_file(t, 500);
    const std::string key = t.read("signing.key");
    auto r = pbtest::run_tool({"sign", "--catalog", t.file("catalog.json")}, {"PBSTORE_SIGNING_KEY=" + key});
    PB_CHECK_EQ(r.code, 0);
    PB_CHECK(t.exists("catalog.json.sig"));
    PB_CHECK(r.out.find(key.substr(0, 44)) == std::string::npos && r.err.find(key.substr(0, 44)) == std::string::npos);
    PB_CHECK_EQ(pbtest::run_tool({"verify", "--catalog", t.file("catalog.json"), "--sig", t.file("catalog.json.sig"), "--keys", t.file("keys")}).code, 0);
    // from a file, and from a differently named variable
    ::unlink(t.file("catalog.json.sig").c_str());
    r = pbtest::run_tool({"sign", "--catalog", t.file("catalog.json"), "--key-file", t.file("signing.key")}, {}, {"PBSTORE_SIGNING_KEY"});
    PB_CHECK_EQ(r.code, 0);
    ::unlink(t.file("catalog.json.sig").c_str());
    r = pbtest::run_tool({"sign", "--catalog", t.file("catalog.json"), "--key-env", "MY_KEY"}, {"MY_KEY=" + key});
    PB_CHECK_EQ(r.code, 0);
    PB_CHECK(t.exists("catalog.json.sig"));
}

PB_TEST(sign_without_a_usable_key_writes_no_signature) {
    pbtest::TmpDir t;
    write_catalog_file(t, 500);
    auto r = pbtest::run_tool({"sign", "--catalog", t.file("catalog.json")}, {}, {"PBSTORE_SIGNING_KEY"});
    PB_CHECK_EQ(r.code, 2);
    PB_CHECK(!t.exists("catalog.json.sig"));
    r = pbtest::run_tool({"sign", "--catalog", t.file("catalog.json")}, {"PBSTORE_SIGNING_KEY=not-a-key"});
    PB_CHECK_EQ(r.code, 2);
    PB_CHECK(r.err.find("not-a-key") == std::string::npos);        // the value is not echoed
    PB_CHECK(!t.exists("catalog.json.sig"));
    r = pbtest::run_tool({"sign", "--catalog", t.file("missing.json")}, {"PBSTORE_SIGNING_KEY=AAAA"});
    PB_CHECK_EQ(r.code, 2);
    r = pbtest::run_tool({"sign", "--catalog", t.file("catalog.json"), "--key-file", t.file("no-such.key")});
    PB_CHECK_EQ(r.code, 2);
}

PB_TEST(verify_has_exit_codes_0_1_2_and_checks_the_rollback) {
    pbtest::TmpDir t;
    make_keys(t);
    write_catalog_file(t, 500);
    pbtest::run_tool({"sign", "--catalog", t.file("catalog.json")}, {"PBSTORE_SIGNING_KEY=" + t.read("signing.key")});
    const std::vector<std::string> base = {"verify", "--catalog", t.file("catalog.json"), "--sig", t.file("catalog.json.sig"), "--keys", t.file("keys")};
    auto with = [&](std::vector<std::string> extra) { auto a = base; a.insert(a.end(), extra.begin(), extra.end()); return a; };
    PB_CHECK_EQ(pbtest::run_tool(base).code, 0);
    PB_CHECK_EQ(pbtest::run_tool(with({"--last-serial", "500"})).code, 0);
    auto rb = pbtest::run_tool(with({"--last-serial", "501"}));
    PB_CHECK_EQ(rb.code, 1);
    PB_CHECK(rb.err.find("error[E-CAT-04]") != std::string::npos);
    // tampered content -> 1 with E-CAT-01
    std::string bytes = t.read("catalog.json");
    bytes[bytes.find("alpha-app")] = 'b';
    t.write("tampered.json", bytes);
    auto rt = pbtest::run_tool({"verify", "--catalog", t.file("tampered.json"), "--sig", t.file("catalog.json.sig"), "--keys", t.file("keys")});
    PB_CHECK_EQ(rt.code, 1);
    PB_CHECK(rt.err.find("error[E-CAT-01]") != std::string::npos);
    // an untrusted key -> 1
    ::mkdir(t.file("other").c_str(), 0755);
    pbtest::run_tool({"keygen", "--private-out", t.file("o.key"), "--public-out", t.file("other/o.pub")});
    PB_CHECK_EQ(pbtest::run_tool({"verify", "--catalog", t.file("catalog.json"), "--sig", t.file("catalog.json.sig"), "--keys", t.file("other")}).code, 1);
    // missing files, missing keys directory, bad numbers, no options -> 2
    PB_CHECK_EQ(pbtest::run_tool({"verify", "--catalog", t.file("none.json"), "--sig", t.file("catalog.json.sig"), "--keys", t.file("keys")}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"verify", "--catalog", t.file("catalog.json"), "--sig", t.file("catalog.json.sig"), "--keys", t.file("nokeys")}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool(with({"--last-serial", "abc"})).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"verify"}).code, 2);
    // an empty keys directory trusts nothing: that is a configuration error, not a verdict
    ::mkdir(t.file("empty").c_str(), 0755);
    PB_CHECK_EQ(pbtest::run_tool({"verify", "--catalog", t.file("catalog.json"), "--sig", t.file("catalog.json.sig"), "--keys", t.file("empty")}).code, 2);
}

PB_TEST(github_flag_prints_annotations_and_version_is_reported) {
    pbtest::TmpDir t;
    make_keys(t);
    write_catalog_file(t, 500);
    pbtest::run_tool({"sign", "--catalog", t.file("catalog.json")}, {"PBSTORE_SIGNING_KEY=" + t.read("signing.key")});
    auto r = pbtest::run_tool({"verify", "--catalog", t.file("catalog.json"), "--sig", t.file("catalog.json.sig"), "--keys", t.file("keys"),
                               "--last-serial", "900", "--github"});
    PB_CHECK_EQ(r.code, 1);
    PB_CHECK(r.out.find("::error") != std::string::npos);
    PB_CHECK(r.out.find("[E-CAT-04]") != std::string::npos);
    auto v = pbtest::run_tool({"--version"});
    PB_CHECK_EQ(v.code, 0);
    PB_CHECK(v.out.find("pbstore-catalog") != std::string::npos);
    PB_CHECK(v.out.find("schema 1.x") != std::string::npos);
}

PB_TEST_MAIN()
