// T015: standard-alphabet base64 with strict decoding.
#include "pbstore/base64.h"
#include "test_support.h"

using namespace pbstore;

PB_TEST(round_trips_for_lengths_0_to_70) {
    for (int n = 0; n <= 70; ++n) {
        std::string data;
        for (int i = 0; i < n; ++i) data.push_back(static_cast<char>((i * 37 + n) & 0xFF));
        const std::string enc = base64_encode(data);
        PB_CHECK_EQ(enc.size() % 4, size_t(0));
        auto dec = base64_decode(enc);
        PB_CHECK(dec.has_value());
        PB_CHECK_EQ(*dec, data);
    }
    PB_CHECK_EQ(base64_encode("Man"), std::string("TWFu"));
    PB_CHECK_EQ(base64_encode("Ma"), std::string("TWE="));
    PB_CHECK_EQ(base64_encode("M"), std::string("TQ=="));
}

PB_TEST(decoding_accepts_one_trailing_newline) {
    PB_CHECK(base64_decode("TWFu\n").has_value());
    PB_CHECK_EQ(*base64_decode("TWFu\n"), std::string("Man"));
    PB_CHECK(!base64_decode("TWFu\n\n").has_value());
    PB_CHECK(!base64_decode("TWFu\r\n").has_value());
}

PB_TEST(rejects_malformed_input) {
    for (const char* bad : {"TWF", "TWFu=", "TW=u", "T===", "====", "TW Fu", "TWF\nu", "TWF-", "TW_u", "TWE", "TQ=",
                            "TWFuTWF", "=TWF", "TWE=TWE=", "\xc3\xa4\xc3\xa4"}) {
        PB_CHECK(!base64_decode(bad).has_value());
    }
}

PB_TEST(rejects_non_canonical_padding_bits) {
    // "TR==" decodes to the same byte as "TQ==" but has non-zero unused bits
    PB_CHECK(base64_decode("TQ==").has_value());
    PB_CHECK(!base64_decode("TR==").has_value());
    PB_CHECK(!base64_decode("TWF=").has_value());
}

PB_TEST_MAIN()
