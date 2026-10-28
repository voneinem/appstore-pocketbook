// T074: what is compiled into the released store program (spec FR-007): the catalog address and the trusted keys of the installer's
// release configuration, the store's own name and version, the release date that is the floor of the clock check, and tags the
// packaging step looks for. No environment variable changes any of it; the test build is a separate host-only target.
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "app/store_config.h"
#include "pbstore/base64.h"
#include "pbstore/install/build_config.h"
#include "test_support.h"

using namespace pbstore;

namespace {
std::string first_line_of(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    return line;
}
}  // namespace

PB_TEST(the_release_configuration_has_the_real_address_and_the_keys_of_keys_trusted) {
    install::Config config = app::make_engine_config();
    PB_CHECK_EQ(config.catalog_url, install::catalog_url());
    PB_CHECK(config.catalog_url.compare(0, 8, "https://") == 0);
    PB_CHECK(config.catalog_url.find("localhost") == std::string::npos);
    PB_CHECK(!config.trusted_keys.empty());
    PB_CHECK(config.trusted_keys.size() <= 3);
    PB_CHECK((config.trusted_keys == install::trusted_keys()));
}

PB_TEST(the_store_says_who_it_is_in_the_install_record) {
    install::Config config = app::make_engine_config();
    PB_CHECK_EQ(config.program_name, std::string("pbstore-app"));
    PB_CHECK_EQ(config.program_version, first_line_of(std::string(PBSTORE_SOURCE_DIR) + "/app/VERSION"));
}

PB_TEST(the_release_date_and_the_running_version_are_those_of_the_files) {
    store::StoreConfig config = app::make_store_config();
    PB_CHECK_EQ(config.release_date, first_line_of(std::string(PBSTORE_SOURCE_DIR) + "/app/RELEASE_DATE"));
    PB_CHECK_EQ(config.running_version, first_line_of(std::string(PBSTORE_SOURCE_DIR) + "/app/VERSION"));
    PB_CHECK_EQ(config.catalog_url, install::catalog_url());
}

PB_TEST(the_tags_for_the_packaging_check_carry_the_same_values) {
    PB_CHECK_EQ(std::string(app::kVersionTag), "PBSTORE_APP_VERSION=" + first_line_of(std::string(PBSTORE_SOURCE_DIR) + "/app/VERSION"));
    PB_CHECK_EQ(std::string(app::kReleaseDateTag), "PBSTORE_RELEASE_DATE=" + first_line_of(std::string(PBSTORE_SOURCE_DIR) + "/app/RELEASE_DATE"));
}

PB_TEST(no_environment_variable_changes_the_address_the_keys_or_anything_else) {
    const std::string url = app::make_engine_config().catalog_url;
    const auto keys = app::make_engine_config().trusted_keys;
    ::setenv("PBSTORE_TEST_CATALOG_URL", "https://evil.example/catalog.json", 1);
    ::setenv("PBSTORE_TEST_CA_FILE", "/tmp/evil.pem", 1);
    ::setenv("PBSTORE_TEST_STATE_DIR", "/tmp/evil", 1);
    ::setenv("PBSTORE_CATALOG_URL", "https://evil.example/catalog.json", 1);
    ::setenv("SSL_CERT_FILE", "/tmp/evil.pem", 1);
    PB_CHECK_EQ(app::make_engine_config().catalog_url, url);
    PB_CHECK((app::make_engine_config().trusted_keys == keys));
    PB_CHECK_EQ(app::make_store_config().catalog_url, url);
    for (const char* v : {"PBSTORE_TEST_CATALOG_URL", "PBSTORE_TEST_CA_FILE", "PBSTORE_TEST_STATE_DIR", "PBSTORE_CATALOG_URL", "SSL_CERT_FILE"}) ::unsetenv(v);
}

PB_TEST_MAIN()
