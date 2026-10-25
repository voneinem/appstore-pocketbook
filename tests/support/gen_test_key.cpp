// gen_test_key OUTDIR: writes a fresh throw-away Ed25519 key pair for the test build of the installer:
//   OUTDIR/test_trusted_key.inc    the public key as a quoted base64 string and a comma (compiled into
//                                  pbstore-install-test as its only trusted key)
//   OUTDIR/test_signing_seed.txt   the seed as base64, one line (read by the tests to sign test catalogs)
// Both files are made in the build directory only. No key material is ever committed.
#include <sys/stat.h>

#include <fstream>
#include <iostream>

#include "pbstore/base64.h"
#include "pbstore/ed25519.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: gen_test_key OUTDIR\n";
        return 2;
    }
    const std::string dir = argv[1];
    ::mkdir(dir.c_str(), 0755);
    const std::string seed = pbstore::generate_seed();
    const std::string pub = pbstore::ed25519_public_from_seed(seed);
    {
        std::ofstream out(dir + "/test_trusted_key.inc", std::ios::binary | std::ios::trunc);
        out << "\"" << pbstore::base64_encode(pub) << "\",\n";
        if (!out) return 2;
    }
    {
        std::ofstream out(dir + "/test_signing_seed.txt", std::ios::binary | std::ios::trunc);
        out << pbstore::base64_encode(seed) << "\n";
        if (!out) return 2;
    }
    return 0;
}
