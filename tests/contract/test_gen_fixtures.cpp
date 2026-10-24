// T040: the helper gen_fixtures writes one anonymized invalid package per rule id (E-PKG-01 … 15);
// nothing is committed, and every file is reported with exactly the rule in its name first.
#include "pbstore/package_check.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <dirent.h>
#include <sys/wait.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

using namespace pbstore;

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

PB_TEST(the_generated_fixtures_cover_every_package_rule) {
    pbtest::TmpDir t;
    const std::string cmd = std::string(PBSTORE_GEN_FIXTURES) + " " + t.path() + " > /dev/null 2>&1";
    PB_CHECK_EQ(std::system(cmd.c_str()), 0);
    std::vector<std::string> names;
    DIR* d = opendir(t.path().c_str());
    PB_CHECK(d != nullptr);
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".zip") == 0) names.push_back(n);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    std::set<std::string> rules;
    for (const std::string& n : names) {
        PB_CHECK_EQ(n.compare(0, 6, "E-PKG-"), 0);
        PB_CHECK(n[8] == '-');
        const std::string rule = n.substr(0, 8);
        PackageCheckResult r = check_package(slurp(t.file(n)));
        PB_CHECK(!r.ok());
        PB_CHECK(!r.findings.empty());
        PB_CHECK_EQ(r.findings[0].rule_id, rule);
        rules.insert(rule);
    }
    PB_CHECK_EQ(rules.size(), size_t(15));
    PB_CHECK(names.size() >= 15);
}

PB_TEST_MAIN()
