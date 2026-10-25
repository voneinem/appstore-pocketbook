#include "pbstore/install/local_input.h"

#include "pbstore/catalog.h"
#include "pbstore/package_check.h"
#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

Package verify_local_package(std::string bytes) {
    Package p;
    p.source = "local";
    p.bytes = std::make_shared<const std::string>(std::move(bytes));
    p.size = p.bytes->size();
    p.sha256 = sha256_hex(*p.bytes);
    PackageCheckResult r = check_package(*p.bytes);
    p.findings = r.findings;
    if (p.findings.empty() && r.manifest) p.manifest = r.manifest;
    return p;
}

LocalPackageResult read_local_package(const std::string& path, const LocalFileReader& read) {
    LocalPackageResult r;
    r.package.source = "local";
    std::string bytes, why;
    if (!read(path, static_cast<uint64_t>(kMaxPackageBytes), &bytes, &why)) {
        r.findings.push_back(make_error("E-INS-27", path, "the package file " + path + " cannot be used: " + why));
        return r;
    }
    r.package = verify_local_package(std::move(bytes));
    r.findings = r.package.findings;
    return r;
}

}  // namespace install
}  // namespace pbstore
