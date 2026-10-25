#pragma once
// The bundled Mozilla CA certificates (PEM), compiled into the program. Used only when no system
// bundle is found (constitution Principle III: trust anchors from the device store when usable,
// otherwise from a CA file bundled with the application).

#include <string>

std::string pbstore_cacert_bundle();
