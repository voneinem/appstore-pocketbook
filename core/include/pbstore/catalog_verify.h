#pragma once
// The consumer side of docs/contracts/catalog-format.md: every program that reads the catalog
// (the store, the installer, the command line, a script) calls verify_catalog and nothing else.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"

namespace pbstore {

struct VerifyResult {
    std::vector<Finding> findings;       // E-CAT-01 … E-CAT-05
    std::optional<Catalog> catalog;      // set only if every check passed
    int64_t serial = 0;                  // the serial of the accepted catalog

    bool ok() const;
};

// Applies the consumer rules in the documented order:
//   1. size (at most 1 MiB)                          E-CAT-05
//   2. signature over the raw bytes against every trusted key (raw 32-byte public keys)   E-CAT-01
//      The content is not interpreted before this step succeeds.
//   3. parse and validate                            E-CAT-02
//   4. schema major (newer: "needs updating")        E-CAT-03
//   5. rollback: serial below `last_accepted_serial` E-CAT-04 (equal is accepted; 0 = none yet)
// `signature_text` is the content of catalog.json.sig (base64, one line, one trailing newline allowed).
VerifyResult verify_catalog(const std::string& catalog_bytes, const std::string& signature_text,
                            const std::vector<std::string>& trusted_keys, int64_t last_accepted_serial);

// keys/trusted/<name>.pub: one line of base64 for a 32-byte Ed25519 public key.
std::optional<std::string> parse_public_key_text(const std::string& text);

// Loads every *.pub file of a directory (sorted by name). On failure (directory missing, a file
// that is not a public key, no key at all) returns an empty list and sets *error.
std::vector<std::string> load_trusted_keys(const std::string& directory, std::string* error);

}  // namespace pbstore
