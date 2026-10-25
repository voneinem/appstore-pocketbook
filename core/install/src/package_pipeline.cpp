#include "pbstore/install/package_pipeline.h"

#include <map>

#include "pbstore/install/catalog_session.h"
#include "pbstore/package_match.h"
#include "pbstore/sha256.h"
#include "pbstore/zip_package.h"

namespace pbstore {
namespace install {

Package verify_catalog_package(const CatalogEntry& entry, std::string bytes) {
    Package p;
    p.source = "catalog";
    p.bytes = std::make_shared<const std::string>(std::move(bytes));
    p.size = p.bytes->size();
    p.sha256 = sha256_hex(*p.bytes);
    PackageMatchResult m = package_match(entry, *p.bytes);
    p.findings = m.findings;
    if (p.findings.empty() && m.manifest) p.manifest = m.manifest;
    return p;
}

Package fetch_catalog_package(const Downloader& download, const CatalogEntry& entry, const DownloadProgress& progress) {
    FetchResult r = download(entry.package_url, static_cast<uint64_t>(entry.package_size), kPackageSeconds, progress);
    if (!r.ok) {
        Package p;
        p.source = "catalog";
        p.findings.push_back(download_finding(r, "the package of " + entry.id, entry.package_url));
        return p;
    }
    return verify_catalog_package(entry, std::move(r.body));
}

struct ZipContent::Impl {
    std::shared_ptr<const std::string> bytes;
    ZipArchive archive;
    std::map<std::string, size_t> index;   // path -> position in archive.entries()
    bool open = false;
};

ZipContent::ZipContent(std::shared_ptr<const std::string> bytes) : impl_(new Impl) {
    impl_->bytes = std::move(bytes);
    impl_->open = impl_->archive.open(*impl_->bytes).empty();
    if (impl_->open) {
        const auto& entries = impl_->archive.entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].is_directory) impl_->index[entries[i].name] = i;
    }
}

ZipContent::~ZipContent() = default;

bool ZipContent::read(const std::string& path, std::string* out) {
    if (!impl_->open) return false;
    auto it = impl_->index.find(path);
    if (it == impl_->index.end()) return false;
    std::string error;
    return impl_->archive.read(impl_->archive.entries()[it->second], 256ull * 1024 * 1024, out, &error);
}

}  // namespace install
}  // namespace pbstore
