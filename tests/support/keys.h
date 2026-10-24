#pragma once
// Throw-away Ed25519 key pairs for tests. Generated in memory; never written into the repository.

#include <string>

#include "pbstore/base64.h"
#include "pbstore/ed25519.h"

namespace pbtest {

struct KeyPair {
    std::string seed;  // 32 raw bytes
    std::string pub;   // 32 raw bytes
    std::string seed_text() const { return pbstore::base64_encode(seed) + "\n"; }  // private key file / secret
    std::string pub_text() const { return pbstore::base64_encode(pub) + "\n"; }    // keys/trusted/<name>.pub
};

inline KeyPair make_keypair() {
    KeyPair k;
    k.seed = pbstore::generate_seed();
    k.pub = pbstore::ed25519_public_from_seed(k.seed);
    return k;
}

// The contents of catalog.json.sig for `bytes`: base64 of the signature, one line.
inline std::string sign_text(const KeyPair& k, const std::string& bytes) {
    return pbstore::base64_encode(pbstore::ed25519_sign(k.seed, bytes)) + "\n";
}

}  // namespace pbtest
