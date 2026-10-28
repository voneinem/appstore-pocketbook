#pragma once
// A place where the engine's caller keeps the last accepted catalog (its exact bytes and signature), so that a
// program with no network can still list the apps (the store app on the reader). The engine stores a catalog
// only after refresh_catalog accepted it, and verifies the cached bytes again, with the same function and the
// built-in keys, every time it lists from them. Without a cache (nullptr) nothing changes.

#include <string>

namespace pbstore {
namespace install {

class CatalogCache {
public:
    virtual ~CatalogCache() = default;
    // The cached bytes, if there are any. Nothing is checked here.
    virtual bool load(std::string* catalog, std::string* signature) = 0;
    // Remembers an accepted catalog. False if it could not be kept (the engine goes on without it).
    virtual bool store(const std::string& catalog, const std::string& signature) = 0;
    // Forgets the cached catalog (it failed its check).
    virtual void discard() = 0;
};

}  // namespace install
}  // namespace pbstore
