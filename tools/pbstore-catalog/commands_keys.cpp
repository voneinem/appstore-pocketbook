// keygen, sign and verify (docs/contracts/cli.md).
#include <openssl/crypto.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>

#include "common.h"
#include "pbstore/base64.h"
#include "pbstore/catalog_verify.h"
#include "pbstore/ed25519.h"

namespace {

using pbcli::Args;

bool path_exists(const std::string& path) {
    struct stat st;
    return ::lstat(path.c_str(), &st) == 0;   // lstat: a dangling symlink counts as existing
}

// True if `path` (not necessarily existing) lies inside a git work tree: some ancestor directory
// holds a ".git" entry.
bool inside_git_work_tree(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::absolute(path, ec).parent_path();
    dir = fs::weakly_canonical(dir, ec);
    while (!dir.empty()) {
        if (fs::exists(dir / ".git", ec)) return true;
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return false;
}

bool write_new_file(const std::string& path, const std::string& data, mode_t mode, std::string* error) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, mode);
    if (fd < 0) {
        *error = "cannot create " + path + ": " + std::strerror(errno);
        return false;
    }
    size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n <= 0) {
            *error = "cannot write " + path;
            ::close(fd);
            ::unlink(path.c_str());
            return false;
        }
        done += static_cast<size_t>(n);
    }
    ::close(fd);
    return true;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

int cmd_keygen(const Args& a) {
    if (!a.has("private-out") || !a.has("public-out"))
        return pbcli::usage_error("keygen needs --private-out FILE and --public-out FILE");
    const std::string priv = a.value("private-out"), pub = a.value("public-out");
    if (path_exists(priv) || path_exists(pub))
        return pbcli::usage_error("refusing to overwrite an existing file (" + (path_exists(priv) ? priv : pub) + ")");
    if (inside_git_work_tree(priv))
        return pbcli::usage_error("refusing to write the private key inside a git work tree; choose a directory outside any repository");
    std::string seed;
    try {
        seed = pbstore::generate_seed();
    } catch (const std::exception& e) {
        return pbcli::usage_error(e.what());
    }
    const std::string pub_raw = pbstore::ed25519_public_from_seed(seed);
    const std::string pub_text = pbstore::base64_encode(pub_raw) + "\n";
    std::string error;
    if (!write_new_file(priv, pbstore::base64_encode(seed) + "\n", 0600, &error)) return pbcli::usage_error(error);
    if (!write_new_file(pub, pub_text, 0644, &error)) {
        ::unlink(priv.c_str());
        return pbcli::usage_error(error);
    }
    std::cout << "public key: " << trim(pub_text) << "\n"
              << "private key written to " << priv << " (mode 0600).\n"
              << "Store the private key as the CI secret PBSTORE_SIGNING_KEY and in an offline backup,\n"
              << "then delete this local copy. Never commit it.\n";
    return 0;
}

int cmd_sign(const Args& a) {
    if (!a.has("catalog")) return pbcli::usage_error("sign needs --catalog FILE");
    const std::string catalog_path = a.value("catalog");
    std::string key_text, error;
    if (a.has("key-file")) {
        if (!pbcli::read_file(a.value("key-file"), &key_text, &error)) return pbcli::usage_error(error);
    } else {
        const std::string name = a.value("key-env", "PBSTORE_SIGNING_KEY");
        const char* v = std::getenv(name.c_str());
        if (!v || !*v) return pbcli::usage_error("no signing key: set the environment variable " + name + " or use --key-file FILE");
        key_text = v;
    }
    auto seed = pbstore::base64_decode(trim(key_text));
    // the key material does not stay in memory longer than needed
    struct Wipe {
        std::string* a; std::optional<std::string>* b;
        ~Wipe() {
            if (!a->empty()) OPENSSL_cleanse(&(*a)[0], a->size());
            if (b->has_value() && !(*b)->empty()) OPENSSL_cleanse(&(**b)[0], (*b)->size());
        }
    } wipe{&key_text, &seed};
    if (!seed || seed->size() != pbstore::kEd25519SeedLen)
        return pbcli::usage_error("the signing key is not a base64-encoded 32-byte seed");
    std::string bytes;
    if (!pbcli::read_file(catalog_path, &bytes, &error)) return pbcli::usage_error(error);
    const std::string sig = pbstore::ed25519_sign(*seed, bytes);
    if (sig.empty()) return pbcli::usage_error("signing failed");
    // never leave a half-written or stale signature next to a catalog
    const std::string out = catalog_path + ".sig", tmp = catalog_path + ".sig.tmp";
    ::unlink(tmp.c_str());
    if (!write_new_file(tmp, pbstore::base64_encode(sig) + "\n", 0644, &error)) return pbcli::usage_error(error);
    if (::rename(tmp.c_str(), out.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return pbcli::usage_error("cannot write " + out);
    }
    std::cout << "signed " << catalog_path << " -> " << out << "\n";
    return 0;
}

bool parse_number(const std::string& s, int64_t* out) {
    if (s.empty() || s.size() > 18) return false;
    int64_t n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    *out = n;
    return true;
}

int cmd_verify(const Args& a) {
    if (!a.has("catalog") || !a.has("sig") || !a.has("keys"))
        return pbcli::usage_error("verify needs --catalog FILE --sig FILE --keys DIR");
    int64_t last = 0, live = -1;
    if (a.has("last-serial") && !parse_number(a.value("last-serial"), &last))
        return pbcli::usage_error("--last-serial must be a non-negative integer");
    if (a.has("live-serial") && !parse_number(a.value("live-serial"), &live))
        return pbcli::usage_error("--live-serial must be a non-negative integer");
    std::string bytes, sig, error;
    if (!pbcli::read_file(a.value("catalog"), &bytes, &error) || !pbcli::read_file(a.value("sig"), &sig, &error))
        return pbcli::usage_error(error);
    const std::vector<std::string> keys = pbstore::load_trusted_keys(a.value("keys"), &error);
    if (keys.empty()) return pbcli::usage_error(error);
    pbstore::VerifyResult r = pbstore::verify_catalog(bytes, sig, keys, last);
    if (r.ok() && live >= 0 && r.serial <= live)
        r.findings.push_back(pbstore::make_error("E-BLD-04", "catalog",
            "serial " + std::to_string(r.serial) + " is not greater than the live serial " + std::to_string(live)));
    pbcli::report(r.findings, a, a.value("catalog"));
    const int code = pbcli::exit_code(r.findings);
    if (code == 0 && r.catalog)
        std::cout << "catalog verified: serial " << r.serial << ", " << r.catalog->apps.size() << " app(s)\n";
    return code;
}

pbcli::CommandRegistrar reg_keygen("keygen", "create an Ed25519 key pair: keygen --private-out FILE --public-out FILE", cmd_keygen);
pbcli::CommandRegistrar reg_sign("sign", "sign a catalog: sign --catalog FILE [--key-env NAME | --key-file FILE]", cmd_sign);
pbcli::CommandRegistrar reg_verify("verify",
    "verify a catalog like a consumer: verify --catalog FILE --sig FILE --keys DIR [--last-serial N] [--live-serial N] [--github]",
    cmd_verify);

}  // namespace
