// T032 (release build): the catalog address and the trusted keys are compiled in; the released program has
// no way to override either (spec FR-006).
#include "pbstore/base64.h"
#include "pbstore/install/build_config.h"
#include "test_support.h"

#include <dirent.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

using namespace pbstore::install;

namespace {
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

PB_TEST(the_trusted_keys_are_the_files_of_keys_trusted) {
    std::set<std::string> expected;
    DIR* d = ::opendir((std::string(PBSTORE_SOURCE_DIR) + "/keys/trusted").c_str());
    PB_CHECK(d != nullptr);
    while (dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".pub") == 0) {
            auto key = pbstore::base64_decode(slurp(std::string(PBSTORE_SOURCE_DIR) + "/keys/trusted/" + n));
            PB_CHECK(key.has_value());
            expected.insert(*key);
        }
    }
    ::closedir(d);
    const std::vector<std::string> keys = trusted_keys();
    PB_CHECK(!keys.empty());
    PB_CHECK(keys.size() <= 3);
    for (const std::string& k : keys) PB_CHECK_EQ(k.size(), size_t(32));
    PB_CHECK(std::set<std::string>(keys.begin(), keys.end()) == expected);
}

PB_TEST(the_catalog_address_is_the_published_one_and_https) {
    PB_CHECK_EQ(catalog_url(), std::string("https://voneinem.github.io/appstore-pocketbook/catalog.json"));
}

PB_TEST(the_environment_cannot_change_the_address_in_the_release_build) {
    ::setenv("PBSTORE_TEST_CATALOG_URL", "https://example.invalid/catalog.json", 1);
    ::setenv("PBSTORE_CATALOG_URL", "https://example.invalid/catalog.json", 1);
    PB_CHECK_EQ(catalog_url(), std::string("https://voneinem.github.io/appstore-pocketbook/catalog.json"));
    ::unsetenv("PBSTORE_TEST_CATALOG_URL");
    ::unsetenv("PBSTORE_CATALOG_URL");
}

PB_TEST_MAIN()
