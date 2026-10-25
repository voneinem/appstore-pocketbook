// T032 (test build): pbstore-install-test takes its only trusted key from a throw-away key made in the
// build directory, and its catalog address, trust anchors file and state folder from the environment.
#define PBSTORE_TEST_BUILD 1   // for the declarations of the test-only functions
#include "pbstore/base64.h"
#include "pbstore/ed25519.h"
#include "pbstore/install/build_config.h"
#include "test_support.h"

#include <fstream>
#include <sstream>

using namespace pbstore::install;

PB_TEST(the_only_trusted_key_is_the_throw_away_test_key) {
    std::ifstream f(PBSTORE_TEST_SEED_FILE);
    PB_CHECK(f.good());
    std::ostringstream ss;
    ss << f.rdbuf();
    auto seed = pbstore::base64_decode(ss.str());
    PB_CHECK(seed.has_value());
    const std::vector<std::string> keys = trusted_keys();
    PB_CHECK_EQ(keys.size(), size_t(1));
    PB_CHECK_EQ(keys[0], pbstore::ed25519_public_from_seed(*seed));
}

PB_TEST(the_environment_sets_address_ca_file_and_state_folder) {
    ::unsetenv("PBSTORE_TEST_CATALOG_URL");
    ::unsetenv("PBSTORE_TEST_CA_FILE");
    ::unsetenv("PBSTORE_TEST_STATE_DIR");
    PB_CHECK_EQ(catalog_url(), std::string("https://voneinem.github.io/appstore-pocketbook/catalog.json"));
    PB_CHECK_EQ(test_ca_file(), std::string());
    PB_CHECK_EQ(test_state_dir(), std::string());
    ::setenv("PBSTORE_TEST_CATALOG_URL", "https://localhost:4443/catalog.json", 1);
    ::setenv("PBSTORE_TEST_CA_FILE", "/tmp/ca.pem", 1);
    ::setenv("PBSTORE_TEST_STATE_DIR", "/tmp/state", 1);
    PB_CHECK_EQ(catalog_url(), std::string("https://localhost:4443/catalog.json"));
    PB_CHECK_EQ(test_ca_file(), std::string("/tmp/ca.pem"));
    PB_CHECK_EQ(test_state_dir(), std::string("/tmp/state"));
}

PB_TEST_MAIN()
