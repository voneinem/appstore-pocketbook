// T025: Ed25519 on OpenSSL: keys from seeds, signing, verifying, hostile inputs.
#include "pbstore/ed25519.h"
#include "test_support.h"

using namespace pbstore;

static std::string unhex(const std::string& h) {
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
    return out;
}

PB_TEST(rfc8032_test_vectors) {
    // RFC 8032 section 7.1, tests 1 and 2
    const std::string seed1 = unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    PB_CHECK_EQ(ed25519_public_from_seed(seed1), unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
    const std::string sig1 = unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    PB_CHECK_EQ(ed25519_sign(seed1, ""), sig1);
    PB_CHECK(ed25519_verify(ed25519_public_from_seed(seed1), "", sig1));

    const std::string seed2 = unhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
    const std::string pub2 = unhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
    const std::string sig2 = unhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
    PB_CHECK_EQ(ed25519_public_from_seed(seed2), pub2);
    PB_CHECK_EQ(ed25519_sign(seed2, std::string("\x72", 1)), sig2);
    PB_CHECK(ed25519_verify(pub2, std::string("\x72", 1), sig2));
}

PB_TEST(key_pair_from_a_seed_is_deterministic) {
    const std::string seed = generate_seed();
    PB_CHECK_EQ(seed.size(), size_t(32));
    PB_CHECK_EQ(ed25519_public_from_seed(seed), ed25519_public_from_seed(seed));
    PB_CHECK_EQ(ed25519_public_from_seed(seed).size(), size_t(32));
    PB_CHECK(generate_seed() != generate_seed());   // random, not constant
    PB_CHECK_EQ(ed25519_sign(seed, "msg"), ed25519_sign(seed, "msg"));  // Ed25519 is deterministic
}

PB_TEST(sign_and_verify_round_trip) {
    const std::string seed = generate_seed();
    const std::string pub = ed25519_public_from_seed(seed);
    for (const std::string& msg : std::vector<std::string>{std::string(), std::string("a"), std::string(1000, 'x'), std::string("\0\1\2", 3)}) {
        const std::string sig = ed25519_sign(seed, msg);
        PB_CHECK_EQ(sig.size(), size_t(64));
        PB_CHECK(ed25519_verify(pub, msg, sig));
    }
}

PB_TEST(flipping_any_bit_of_message_or_signature_fails) {
    const std::string seed = generate_seed();
    const std::string pub = ed25519_public_from_seed(seed);
    const std::string msg = "the catalog bytes";
    const std::string sig = ed25519_sign(seed, msg);
    for (size_t i = 0; i < msg.size(); ++i)
        for (int bit = 0; bit < 8; ++bit) {
            std::string m = msg;
            m[i] = static_cast<char>(m[i] ^ (1 << bit));
            PB_CHECK(!ed25519_verify(pub, m, sig));
        }
    for (size_t i = 0; i < sig.size(); ++i)
        for (int bit = 0; bit < 8; ++bit) {
            std::string s = sig;
            s[i] = static_cast<char>(s[i] ^ (1 << bit));
            PB_CHECK(!ed25519_verify(pub, msg, s));
        }
}

PB_TEST(a_different_public_key_fails) {
    const std::string a = generate_seed(), b = generate_seed();
    const std::string sig = ed25519_sign(a, "m");
    PB_CHECK(!ed25519_verify(ed25519_public_from_seed(b), "m", sig));
}

PB_TEST(wrong_lengths_are_rejected_without_crashing) {
    const std::string seed = generate_seed();
    const std::string pub = ed25519_public_from_seed(seed);
    const std::string sig = ed25519_sign(seed, "m");
    PB_CHECK(!ed25519_verify(pub, "m", sig.substr(1)));
    PB_CHECK(!ed25519_verify(pub, "m", sig + "x"));
    PB_CHECK(!ed25519_verify(pub, "m", ""));
    PB_CHECK(!ed25519_verify(pub.substr(1), "m", sig));
    PB_CHECK(!ed25519_verify(pub + "x", "m", sig));
    PB_CHECK(!ed25519_verify("", "m", sig));
    PB_CHECK_EQ(ed25519_public_from_seed("short"), std::string());
    PB_CHECK_EQ(ed25519_sign("short", "m"), std::string());
    PB_CHECK_EQ(ed25519_sign(seed + "x", "m"), std::string());
}

PB_TEST_MAIN()
