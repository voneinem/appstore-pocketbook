#include "app/store_config.h"

#include "pbstore/install/build_config.h"

#ifndef PBSTORE_RELEASE_DATE
#error "PBSTORE_RELEASE_DATE is defined by the build from app/RELEASE_DATE"
#endif
#ifndef PBSTORE_APP_VERSION
#error "PBSTORE_APP_VERSION is defined by the build from app/VERSION"
#endif

namespace pbstore {
namespace app {

// Referenced by the program so that the linker keeps them; the packaging check reads them out of the binary.
extern const char kVersionTag[] = "PBSTORE_APP_VERSION=" PBSTORE_APP_VERSION;
extern const char kReleaseDateTag[] = "PBSTORE_RELEASE_DATE=" PBSTORE_RELEASE_DATE;

#ifdef PBSTORE_DEV_BUILD
// A dev or test build for the device says so, so that scripts/check-store-binary.sh can refuse it in a release package.
extern const char kDevBuildMarker[] = "PBSTORE_DEV_BUILD_MARKER";
#endif

install::Config make_engine_config() {
    install::Config config;
    config.catalog_url = install::catalog_url();
    config.trusted_keys = install::trusted_keys();
    config.program_name = "pbstore-app";
    config.program_version = PBSTORE_APP_VERSION;
    return config;
}

store::StoreConfig make_store_config() {
    store::StoreConfig config;
    config.release_date = PBSTORE_RELEASE_DATE;
    config.running_version = PBSTORE_APP_VERSION;
    config.catalog_url = install::catalog_url();
    return config;
}

}  // namespace app
}  // namespace pbstore
