#pragma once
// What the store program is built with: the engine's Config from the installer's release configuration (the real catalog address and
// the keys of keys/trusted/, with no override: the test build is a separate host-only target), the store's own name and version for
// the install record, and the release date that is the floor of the clock check. The tags below are strings the packaging step looks
// for in the finished program (scripts/check-store-binary.sh). Qt-free.

#include "pbstore/install/engine.h"
#include "pbstore/store/store_service.h"

namespace pbstore {
namespace app {

// "PBSTORE_APP_VERSION=<app/VERSION>" and "PBSTORE_RELEASE_DATE=<app/RELEASE_DATE>", compiled in.
extern const char kVersionTag[];
extern const char kReleaseDateTag[];

install::Config make_engine_config();
store::StoreConfig make_store_config();

}  // namespace app
}  // namespace pbstore
