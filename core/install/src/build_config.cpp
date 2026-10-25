// Compiled into each program, not part of the pbstore_install library (see core/install/CMakeLists.txt).
#include "pbstore/install/build_config.h"

#include <cstdlib>

#include "pbstore/base64.h"

namespace pbstore {
namespace install {

namespace {

const char* const kKeys[] = {
#ifdef PBSTORE_TEST_BUILD
#include "test_trusted_key.inc"
#else
#include "trusted_keys.inc"
#endif
};

}  // namespace

std::vector<std::string> trusted_keys() {
    std::vector<std::string> keys;
    for (const char* text : kKeys) {
        auto key = base64_decode(text);
        if (key && key->size() == 32) keys.push_back(*key);
    }
    return keys;
}

std::string catalog_url() {
#ifdef PBSTORE_TEST_BUILD
    if (const char* url = std::getenv("PBSTORE_TEST_CATALOG_URL")) return url;
#endif
    return PBSTORE_CATALOG_URL;
}

#ifdef PBSTORE_TEST_BUILD
std::string test_ca_file() {
    const char* v = std::getenv("PBSTORE_TEST_CA_FILE");
    return v ? v : "";
}

std::string test_state_dir() {
    const char* v = std::getenv("PBSTORE_TEST_STATE_DIR");
    return v ? v : "";
}
#endif

}  // namespace install
}  // namespace pbstore
