#include "pbstore/rules.h"

namespace pbstore {

const std::vector<RuleInfo>& rule_registry() {
    static const std::vector<RuleInfo> rules = {
        {"E-ENT-01", "Entry file is not valid JSON, not an object, or too large"},
        {"E-ENT-02", "Entry field missing, of the wrong type, or not allowed in an entry file"},
        {"E-ENT-03", "Entry schema_version is malformed or a newer major than supported"},
        {"E-ENT-04", "Entry id is malformed or differs from the file name"},
        {"E-ENT-05", "package_url is not a GitHub release asset link"},
        {"E-ENT-06", "License is missing or not on the accepted open-source list"},
        {"E-ENT-07", "A URL field is not an https:// URL"},
        {"E-ENT-08", "A description is missing its English text or is outside its length limit"},
        {"E-ENT-09", "release_date is not a valid date or lies too far in the future"},
        {"E-ENT-10", "Author is empty or too long"},
        {"E-ENT-11", "A withdrawn version is not a valid version or was never listed"},
        {"E-ENT-12", "The package could not be downloaded"},
        {"E-ENT-13", "Manifest id differs from the entry or catalog id"},
        {"E-ENT-14", "Release tag differs from the manifest version"},
        {"E-ENT-15", "Package hash or size differs from the catalog"},
        {"E-ENT-16", "Download link and source link name different GitHub repositories"},
        {"E-ENT-17", "Manifest version, firmware, device or menu entry id differs from the catalog entry"},
        {"E-PKG-01", "Not a readable ZIP, or an encrypted or unsupported entry"},
        {"E-PKG-02", "A package limit is exceeded (size, entries, ratio)"},
        {"E-PKG-03", "Unsafe, duplicate or case-colliding file name, or a symbolic link"},
        {"E-PKG-04", "Something other than manifest.json and applications/ at the ZIP root"},
        {"E-PKG-05", "manifest.json is missing, too large or not valid JSON"},
        {"E-PKG-06", "Manifest schema_version is malformed or a newer major than supported"},
        {"E-PKG-07", "A required manifest field is missing or has the wrong type"},
        {"E-PKG-08", "A manifest id, name, version, firmware or device value is malformed"},
        {"E-PKG-09", "A files path is malformed, outside applications/, or duplicated"},
        {"E-PKG-10", "A listed file is missing from the ZIP"},
        {"E-PKG-11", "A file under applications/ is not listed in the manifest"},
        {"E-PKG-12", "A file's SHA-256 or size differs from the manifest"},
        {"E-PKG-13", "A menu entry path or icon is not among the listed files"},
        {"E-PKG-14", "Menu entry id, title or group is malformed"},
        {"E-PKG-15", "data_dir is malformed, inside applications/, or reserved"},
        {"E-CAT-01", "Catalog signature does not verify against any trusted key"},
        {"E-CAT-02", "Catalog is malformed"},
        {"E-CAT-03", "Catalog schema is newer than this program supports: update needed"},
        {"E-CAT-04", "Catalog serial is lower than the last accepted one (rollback)"},
        {"E-CAT-05", "Catalog is larger than 1 MiB"},
        {"E-BLD-01", "Two apps claim the same menu entry id"},
        {"E-BLD-02", "Two apps claim the same data folder"},
        {"E-BLD-03", "Live catalog is missing or cannot be verified"},
        {"E-BLD-04", "New serial is not greater than the live serial"},
        {"E-BLD-05", "Entry changed to a package whose version is withdrawn"},
        {"E-BLD-06", "Resulting catalog is larger than 1 MiB"},
        {"E-BLD-07", "An entry failed its checks at build time, nothing is produced"},
        {"W-ENT-01", "Owner or repository of a link changed compared with the live catalog"},
        {"W-ENT-02", "Entry text looks like it contains an e-mail address"},
        {"W-PR-01", "Entry files changed together with code, workflow, key or list files"},
    };
    return rules;
}

bool is_known_rule(const std::string& id) {
    for (const RuleInfo& r : rule_registry())
        if (id == r.id) return true;
    return false;
}

Finding make_error(const std::string& rule_id, const std::string& subject, const std::string& message) {
    return Finding{Severity::Error, rule_id, subject, message};
}

Finding make_notice(const std::string& rule_id, const std::string& subject, const std::string& message) {
    return Finding{Severity::Notice, rule_id, subject, message};
}

std::string format_finding(const Finding& f) {
    return std::string(f.severity == Severity::Error ? "error" : "notice") + "[" + f.rule_id + "] " + f.subject +
           ": " + f.message;
}

namespace {
std::string escape_data(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '%') out += "%25";
        else if (c == '\r') out += "%0D";
        else if (c == '\n') out += "%0A";
        else out.push_back(c);
    }
    return out;
}
std::string escape_property(const std::string& s) {
    std::string out;
    for (char c : escape_data(s)) {
        if (c == ':') out += "%3A";
        else if (c == ',') out += "%2C";
        else out.push_back(c);
    }
    return out;
}
}  // namespace

std::string github_annotation(const Finding& f, const std::string& file) {
    std::string out = f.severity == Severity::Error ? "::error" : "::notice";
    if (!file.empty()) out += " file=" + escape_property(file);
    out += "::[" + f.rule_id + "] " + f.subject + ": " + escape_data(f.message);
    return out;
}

}  // namespace pbstore
