#pragma once
// What is compiled into each program: the trusted public keys and the address of the signed catalog. The
// released program has no way to change either. Only the test build (pbstore-install-test, never shipped)
// reads overrides, from the environment, so the tests can drive it with a throw-away key and a local server.

#include <string>
#include <vector>

namespace pbstore {
namespace install {

// Raw 32-byte Ed25519 public keys (keys/trusted/*.pub at build time; at most three).
std::vector<std::string> trusted_keys();
std::string catalog_url();

#ifdef PBSTORE_TEST_BUILD
// A PEM file of trust anchors for the local test server; empty if not set.
std::string test_ca_file();
// A folder to use instead of the user's state folder; empty if not set.
std::string test_state_dir();
#endif

}  // namespace install
}  // namespace pbstore
