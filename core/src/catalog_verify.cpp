#include "pbstore/catalog_verify.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "pbstore/base64.h"
#include "pbstore/ed25519.h"

namespace pbstore {

bool VerifyResult::ok() const {
    if (!catalog.has_value()) return false;
    for (const Finding& f : findings)
        if (f.severity == Severity::Error) return false;
    return true;
}

VerifyResult verify_catalog(const std::string& bytes, const std::string& signature_text,
                            const std::vector<std::string>& trusted_keys, int64_t last_accepted_serial) {
    VerifyResult r;
    // 1. size
    if (bytes.size() > kMaxCatalogBytes) {
        r.findings.push_back(make_error("E-CAT-05", "catalog",
            "catalog is " + std::to_string(bytes.size()) + " bytes, the limit is " + std::to_string(kMaxCatalogBytes)));
        return r;
    }
    // 2. signature, before anything is interpreted
    const auto sig = base64_decode(signature_text);
    bool signed_ok = false;
    if (sig && sig->size() == kEd25519SignatureLen) {
        for (const std::string& key : trusted_keys)
            if (ed25519_verify(key, bytes, *sig)) { signed_ok = true; break; }
    }
    if (!signed_ok) {
        r.findings.push_back(make_error("E-CAT-01", "catalog",
            sig && sig->size() == kEd25519SignatureLen
                ? "signature does not verify against any of the " + std::to_string(trusted_keys.size()) + " trusted key(s)"
                : "signature file is not a valid base64 Ed25519 signature"));
        return r;
    }
    // 3. + 4. parse, validate, schema
    CatalogParseResult parsed = parse_catalog(bytes);
    if (!parsed.catalog) {
        r.findings = parsed.findings;
        return r;
    }
    // 5. rollback
    if (parsed.catalog->serial < last_accepted_serial) {
        r.findings.push_back(make_error("E-CAT-04", "catalog",
            "serial " + std::to_string(parsed.catalog->serial) + " is lower than the last accepted serial " +
            std::to_string(last_accepted_serial) + " (rollback)"));
        return r;
    }
    r.serial = parsed.catalog->serial;
    r.catalog = std::move(parsed.catalog);
    return r;
}

std::optional<std::string> parse_public_key_text(const std::string& text) {
    auto raw = base64_decode(text);
    if (!raw || raw->size() != kEd25519PublicKeyLen) return std::nullopt;
    return raw;
}

std::vector<std::string> load_trusted_keys(const std::string& directory, std::string* error) {
    namespace fs = std::filesystem;
    std::vector<std::string> keys;
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        if (error) *error = "trusted keys directory not found: " + directory;
        return {};
    }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(directory, ec))
        if (e.is_regular_file() && e.path().extension() == ".pub") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        std::ifstream f(p, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        auto key = parse_public_key_text(ss.str());
        if (!key) {
            if (error) *error = p.filename().string() + " is not a public key (expected one line of base64, 32 bytes)";
            return {};
        }
        keys.push_back(*key);
    }
    if (keys.empty()) {
        if (error) *error = "no *.pub files in " + directory;
        return {};
    }
    return keys;
}

}  // namespace pbstore
