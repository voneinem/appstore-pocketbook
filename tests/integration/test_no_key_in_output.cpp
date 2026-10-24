// T087: the signing key never appears in any output or in any file other than the key file that
// keygen creates (constitution Principles III and V).
#include "pbstore/base64.h"
#include "entry_builder.h"
#include "keys.h"
#include "run_tool.h"
#include "sample_app.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <dirent.h>
#include <sys/stat.h>

using namespace pbstore;

static bool contains(const std::string& text, const std::string& needle) { return !needle.empty() && text.find(needle) != std::string::npos; }

// every regular file below `dir`, read completely
static std::string all_files_text(const std::string& dir) {
    std::string out;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            const std::string p = dir + "/" + n;
            struct stat st;
            if (::stat(p.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) out += all_files_text(p);
            else { std::string s; FILE* f = fopen(p.c_str(), "rb"); if (f) { char buf[4096]; size_t k; while ((k = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, k); fclose(f); } out += "\n#" + n + "\n" + s; }
        }
        closedir(d);
    }
    return out;
}

PB_TEST(no_command_prints_the_signing_key_and_no_file_holds_it_except_the_key_file) {
    pbtest::TmpDir t;
    for (const char* d : {"entries", "packages", "keys", "out", "work"}) ::mkdir(t.file(d).c_str(), 0755);
    auto gen = pbtest::run_tool({"keygen", "--private-out", t.file("work/signing.key"), "--public-out", t.file("keys/current.pub")});
    PB_CHECK_EQ(gen.code, 0);
    const std::string key = t.read("work/signing.key");
    const std::string key_core = key.substr(0, 44);                  // the 44 base64 characters of the seed
    PB_CHECK(!contains(gen.out, key_core) && !contains(gen.err, key_core));

    t.write("entries/alpha-app.json", json::write(pbtest::sample_entry_doc("alpha-app", "1.2.0")));
    t.write("packages/alpha-app-inkpad-one-1.2.0.zip", pbtest::build_package(pbtest::make_sample_app("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app", 3)));
    const std::vector<std::string> env = {"PBSTORE_SIGNING_KEY=" + key};
    std::vector<pbtest::RunResult> runs;
    runs.push_back(pbtest::run_tool({"build", "--entries", t.file("entries"), "--out", t.file("out"), "--package-dir", t.file("packages"), "--first-publish", "--now", "1000"}, env));
    runs.push_back(pbtest::run_tool({"sign", "--catalog", t.file("out/catalog.json")}, env));
    runs.push_back(pbtest::run_tool({"sign", "--catalog", t.file("out/catalog.json"), "--key-file", t.file("work/signing.key")}, env));
    runs.push_back(pbtest::run_tool({"verify", "--catalog", t.file("out/catalog.json"), "--sig", t.file("out/catalog.json.sig"), "--keys", t.file("keys"), "--github"}, env));
    runs.push_back(pbtest::run_tool({"check-package", t.file("packages/alpha-app-inkpad-one-1.2.0.zip"), "--github"}, env));
    runs.push_back(pbtest::run_tool({"check-entry", t.file("entries/alpha-app.json"), "--package", t.file("packages/alpha-app-inkpad-one-1.2.0.zip"), "--github"}, env));
    runs.push_back(pbtest::run_tool({"sign", "--catalog", t.file("does-not-exist.json")}, env));             // an error path
    runs.push_back(pbtest::run_tool({"sign", "--catalog", t.file("out/catalog.json"), "--key-env", "NOT_SET"}, env));
    runs.push_back(pbtest::run_tool({"--version"}, env));
    runs.push_back(pbtest::run_tool({"--help"}, env));
    for (const auto& r : runs) PB_CHECK(!contains(r.out, key_core) && !contains(r.err, key_core));
    PB_CHECK_EQ(runs[0].code, 0);
    PB_CHECK_EQ(runs[1].code, 0);

    // files: the key is only in the key file
    const std::string seed_b64 = key_core;
    for (const char* dir : {"entries", "packages", "keys", "out"}) PB_CHECK(!contains(all_files_text(t.file(dir)), seed_b64));
    // the signing key variable is not copied into a temporary file next to the catalog
    PB_CHECK(!t.exists("out/catalog.json.sig.tmp"));
    // a malformed value is not echoed back either
    auto bad = pbtest::run_tool({"sign", "--catalog", t.file("out/catalog.json")}, {"PBSTORE_SIGNING_KEY=super-secret-value-12345"});
    PB_CHECK_EQ(bad.code, 2);
    PB_CHECK(!contains(bad.out, "super-secret-value-12345") && !contains(bad.err, "super-secret-value-12345"));
}

PB_TEST_MAIN()
