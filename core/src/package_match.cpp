#include "pbstore/package_match.h"

#include "pbstore/package_check.h"
#include "pbstore/sha256.h"

namespace pbstore {

bool PackageMatchResult::ok() const {
    if (!manifest.has_value()) return false;
    for (const Finding& f : findings)
        if (f.severity == Severity::Error) return false;
    return true;
}

PackageMatchResult package_match(const CatalogEntry& entry, const std::string& bytes) {
    PackageMatchResult r;
    const std::string subject = entry.id;
    const std::string actual_sha = sha256_hex(bytes);
    if (static_cast<int64_t>(bytes.size()) != entry.package_size)
        r.findings.push_back(make_error("E-ENT-15", subject, "package size differs (catalog " + std::to_string(entry.package_size) + ", actual " + std::to_string(bytes.size()) + ")"));
    if (actual_sha != entry.package_sha256)
        r.findings.push_back(make_error("E-ENT-15", subject, "package SHA-256 differs (catalog " + entry.package_sha256 + ", actual " + actual_sha + ")"));
    if (!r.findings.empty()) return r;

    PackageCheckResult pc = check_package(bytes);
    r.findings = pc.findings;
    if (!pc.manifest) return r;
    const Manifest& m = *pc.manifest;
    bool differs = false;
    auto differ = [&](const char* rule, const std::string& field, const std::string& manifest_value, const std::string& catalog_value) {
        r.findings.push_back(make_error(rule, subject, field + ": manifest has \"" + manifest_value + "\" but the catalog entry has \"" + catalog_value + "\""));
        differs = true;
    };
    if (m.id != entry.id) differ("E-ENT-13", "id", m.id, entry.id);
    if (m.version != entry.version) differ("E-ENT-17", "version", m.version, entry.version);
    if (m.min_firmware != entry.min_firmware) differ("E-ENT-17", "min_firmware", m.min_firmware, entry.min_firmware);
    if (m.target_device != entry.target_device) differ("E-ENT-17", "target_device", m.target_device, entry.target_device);
    if (m.menu_entry.id != entry.menu_entry_id) differ("E-ENT-17", "menu_entry.id", m.menu_entry.id, entry.menu_entry_id);
    if (m.data_dir != entry.data_dir) differ("E-ENT-17", "data_dir", m.data_dir.value_or("(none)"), entry.data_dir.value_or("(none)"));
    if (!differs) r.manifest = pc.manifest;
    return r;
}

}  // namespace pbstore
