// T014: SHA-256 (one-shot and incremental) and the strict lowercase hex form.
#include "pbstore/sha256.h"
#include "test_support.h"

using namespace pbstore;

PB_TEST(known_vectors) {
    PB_CHECK_EQ(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    PB_CHECK_EQ(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    PB_CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    PB_CHECK_EQ(sha256_hex(std::string(1000000, 'a')),
                std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

PB_TEST(streaming_in_uneven_chunks_equals_one_shot) {
    std::string data;
    for (int i = 0; i < 10007; ++i) data.push_back(static_cast<char>((i * 31 + 7) & 0xFF));
    const std::string expected = sha256_hex(data);
    Sha256 h;
    size_t pos = 0, step = 1;
    while (pos < data.size()) {
        const size_t n = std::min(step, data.size() - pos);
        h.update(data.data() + pos, n);
        pos += n;
        step = step * 3 + 1;
    }
    PB_CHECK_EQ(h.finish_hex(), expected);
}

PB_TEST(output_is_64_lowercase_hex_characters) {
    const std::string h = sha256_hex("x");
    PB_CHECK_EQ(h.size(), size_t(64));
    for (char c : h) PB_CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
}

PB_TEST(hex_parser_is_strict) {
    const std::string good = sha256_hex("abc");
    auto bytes = parse_sha256_hex(good);
    PB_CHECK(bytes.has_value());
    PB_CHECK_EQ(static_cast<int>((*bytes)[0]), 0xba);
    PB_CHECK_EQ(static_cast<int>((*bytes)[31]), 0xad);
    std::string upper = good;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    PB_CHECK(!parse_sha256_hex(upper).has_value());
    PB_CHECK(!parse_sha256_hex(good.substr(1)).has_value());
    PB_CHECK(!parse_sha256_hex(good + "0").has_value());
    PB_CHECK(!parse_sha256_hex(std::string(63, 'a') + "g").has_value());
    PB_CHECK(!parse_sha256_hex("").has_value());
    PB_CHECK(is_sha256_hex(good));
    PB_CHECK(!is_sha256_hex(upper));
}

PB_TEST_MAIN()
