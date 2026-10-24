#include "pbstore/package_check.h"

#include <map>
#include <set>

#include "pbstore/sha256.h"

namespace pbstore {

bool PackageCheckResult::ok() const {
    if (!manifest.has_value()) return false;
    for (const Finding& f : findings)
        if (f.severity == Severity::Error) return false;
    return true;
}

namespace {

std::string shown(const std::string& s) {
    std::string t = s.size() > 80 ? s.substr(0, 77) + "..." : s;
    for (char& c : t)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) c = '?';
    return t;
}

}  // namespace

PackageCheckResult check_package(const std::string& zip_bytes, const ZipLimits& limits) {
    PackageCheckResult r;
    r.package_size = static_cast<int64_t>(zip_bytes.size());
    r.package_sha256 = sha256_hex(zip_bytes);

    ZipArchive zip;
    r.findings = zip.open(zip_bytes, limits);
    if (!r.findings.empty()) return r;

    // strict root: only manifest.json and the applications/ tree
    const ZipEntry* manifest_entry = nullptr;
    for (const ZipEntry& e : zip.entries()) {
        std::string path = e.is_directory ? e.name.substr(0, e.name.size() - 1) : e.name;
        const std::string first = path.substr(0, path.find('/'));
        if (path == "manifest.json" && !e.is_directory) { manifest_entry = &e; continue; }
        if (first == "applications" && (path.size() > first.size() || e.is_directory)) continue;
        r.findings.push_back(make_error("E-PKG-04", shown(e.name),
            "\"" + shown(e.name) + "\" is not allowed at the package root (only manifest.json and the applications/ tree)"));
    }

    // manifest.json
    if (!manifest_entry) {
        r.findings.push_back(make_error("E-PKG-05", "manifest.json", "manifest.json is missing from the package root"));
        return r;
    }
    if (manifest_entry->size > kMaxManifestBytes) {
        r.findings.push_back(make_error("E-PKG-05", "manifest.json", "manifest.json is " + std::to_string(manifest_entry->size) +
                                        " bytes, the limit is " + std::to_string(kMaxManifestBytes)));
        return r;
    }
    std::string manifest_text, error;
    if (!zip.read(*manifest_entry, kMaxManifestBytes, &manifest_text, &error)) {
        r.findings.push_back(make_error("E-PKG-05", "manifest.json", "manifest.json cannot be read: " + error));
        return r;
    }
    ManifestParseResult parsed = parse_manifest(manifest_text);
    for (const Finding& f : parsed.findings) r.findings.push_back(f);
    if (!parsed.manifest) return r;     // an unusable manifest: nothing to compare the files against

    // the file list against the real content
    std::map<std::string, const ZipEntry*> in_zip;
    for (const ZipEntry& e : zip.entries())
        if (!e.is_directory && e.name.rfind("applications/", 0) == 0) in_zip[e.name] = &e;
    std::set<std::string> listed;
    for (const FileEntry& f : parsed.manifest->files) {
        listed.insert(f.path);
        auto it = in_zip.find(f.path);
        if (it == in_zip.end()) {
            r.findings.push_back(make_error("E-PKG-10", f.path, "listed in the manifest but missing from the package"));
            continue;
        }
        std::string actual_sha;
        uint64_t actual_size = 0;
        if (!zip.digest(*it->second, &actual_sha, &actual_size, &error)) {
            r.findings.push_back(make_error("E-PKG-01", f.path, "cannot be read: " + error));
            continue;
        }
        if (static_cast<int64_t>(actual_size) != f.size)
            r.findings.push_back(make_error("E-PKG-12", f.path, "size differs (manifest " + std::to_string(f.size) + ", actual " + std::to_string(actual_size) + ")"));
        else if (actual_sha != f.sha256)
            r.findings.push_back(make_error("E-PKG-12", f.path, "SHA-256 differs (manifest " + f.sha256 + ", actual " + actual_sha + ")"));
    }
    for (const auto& kv : in_zip)
        if (!listed.count(kv.first))
            r.findings.push_back(make_error("E-PKG-11", shown(kv.first), "present in the package but not listed in the manifest"));

    r.manifest = std::move(parsed.manifest);
    return r;
}

}  // namespace pbstore
