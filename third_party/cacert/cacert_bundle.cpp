#include "pbstore_cacert.h"

namespace {
const unsigned char kBundle[] = {
#include "cacert_bundle.inc"
    0};
}

std::string pbstore_cacert_bundle() {
    return std::string(reinterpret_cast<const char*>(kBundle), sizeof kBundle - 1);
}
