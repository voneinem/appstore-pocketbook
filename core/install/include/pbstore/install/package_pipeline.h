#pragma once
// A package, checked in a fixed order before any of it is used: size limit while downloading, SHA-256 and
// size against the signed entry, then the ZIP and manifest rules, then the cross-checks. The verified bytes
// are held once and are the only source of what is written to the reader.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/install/content.h"
#include "pbstore/install/net.h"
#include "pbstore/manifest.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

struct Package {
    std::shared_ptr<const std::string> bytes;   // the ZIP, held once
    std::string sha256;
    uint64_t size = 0;
    std::optional<Manifest> manifest;           // set only if every check passed
    std::string source;                         // "catalog" or "local"
    std::vector<Finding> findings;              // every finding, in the order the checks ran
    bool ok() const { return manifest.has_value() && findings.empty(); }
};

// Checks the bytes of a package announced by a (signed) catalog entry.
Package verify_catalog_package(const CatalogEntry& entry, std::string bytes);

// Downloads the package of an entry (limit: the signed size; address: the one in the signed entry) and checks it.
Package fetch_catalog_package(const Downloader& download, const CatalogEntry& entry, const DownloadProgress& progress);

// Reads files out of the verified ZIP.
class ZipContent : public PackageContent {
public:
    explicit ZipContent(std::shared_ptr<const std::string> bytes);
    ~ZipContent() override;
    bool read(const std::string& path, std::string* out) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace install
}  // namespace pbstore
