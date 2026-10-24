// T052: check-pr-paths: entries changed together with code, workflow, key or list files.
#include "pbstore/pr_paths.h"
#include "test_support.h"

using namespace pbstore;

static bool notice(const std::vector<Finding>& f, const char* needle = "") {
    for (const Finding& x : f)
        if (x.rule_id == "W-PR-01" && x.severity == Severity::Notice && x.message.find(needle) != std::string::npos) return true;
    return false;
}

PB_TEST(entries_alone_need_no_notice) {
    PB_CHECK(check_pr_paths({"catalog/entries/sample-app.json"}).empty());
    PB_CHECK(check_pr_paths({"catalog/entries/a.json", "catalog/entries/b.json"}).empty());
    PB_CHECK(check_pr_paths({}).empty());
}

PB_TEST(code_alone_needs_no_entry_notice) {
    PB_CHECK(check_pr_paths({"core/src/json.cpp", ".github/workflows/publish.yml", "keys/trusted/a.pub"}).empty());
}

PB_TEST(entries_together_with_sensitive_paths_are_a_notice) {
    for (const char* p : {"core/src/json.cpp", "tools/pbstore-catalog/main.cpp", "scripts/pre-merge.sh", ".github/workflows/publish.yml",
                          "keys/trusted/current.pub", "third_party/miniz/miniz.c", "catalog/accepted-licenses.txt",
                          "catalog/reserved-data-dirs.txt", "CMakeLists.txt", "tests/unit/test_json.cpp"}) {
        std::vector<Finding> f = check_pr_paths({"catalog/entries/sample-app.json", p});
        const std::string label = std::string("path ") + p;
        if (!notice(f, p)) throw pbtest::Failure{"no notice naming " + label};
        PB_CHECK(f[0].message.find("own code") != std::string::npos);   // says the check ran with the pull request's code
    }
}

PB_TEST(documentation_and_other_files_do_not_trigger_the_notice) {
    PB_CHECK(check_pr_paths({"catalog/entries/sample-app.json", "docs/developer-guide.md", "README.md"}).empty());
}

PB_TEST(path_names_with_spaces_and_dotdot_are_handled) {
    // a path that climbs out of catalog/entries/ is not an entry file
    std::vector<Finding> f = check_pr_paths({"catalog/entries/a.json", "catalog/entries/../../core/x.cpp"});
    PB_CHECK(notice(f));
    PB_CHECK(check_pr_paths({"catalog/entries/my app.json"}).empty());
    PB_CHECK(!notice(check_pr_paths({"catalog/entries/../../docs/x.md"})));     // not an entry, not sensitive
    PB_CHECK(check_pr_paths({"", "/", "..", "../..", "catalog/entries/", "catalog/entries/sub/x.json"}).size() <= 1);
    // a nested file below catalog/entries/ is not an entry file
    PB_CHECK(check_pr_paths({"catalog/entries/sub/x.json", "core/a.cpp"}).empty());
}

PB_TEST(at_most_ten_sensitive_paths_are_listed) {
    std::vector<std::string> paths = {"catalog/entries/a.json"};
    for (int i = 0; i < 30; ++i) paths.push_back("core/src/f" + std::to_string(i) + ".cpp");
    std::vector<Finding> f = check_pr_paths(paths);
    PB_CHECK_EQ(f.size(), size_t(1));
    PB_CHECK(f[0].message.find("core/src/f0.cpp") != std::string::npos);
    PB_CHECK(f[0].message.find("core/src/f29.cpp") == std::string::npos);
    PB_CHECK(f[0].message.find("30") != std::string::npos);                    // the total is named
}

PB_TEST_MAIN()
