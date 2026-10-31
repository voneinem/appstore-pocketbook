// T065: untrusted text (names, descriptions, file names of a catalog or a package) is shown as plain text: control characters,
// terminal escapes and invisible direction controls removed, cut at the limits of the catalog contract (spec FR-011).
#include "pbstore/store/store_text.h"
#include "test_support.h"

using namespace pbstore::store;

PB_TEST(plain_text_is_unchanged) {
    PB_CHECK_EQ(store_text("PB Notes 1.2", 40), std::string("PB Notes 1.2"));
    PB_CHECK_EQ(store_text("", 40), std::string(""));
    PB_CHECK_EQ(store_text("Zażółć gęślą jaźń \xE2\x80\x94 ok", 40), std::string("Zażółć gęślą jaźń \xE2\x80\x94 ok"));   // multi-byte text survives
}

PB_TEST(control_characters_are_removed_with_no_exception) {
    PB_CHECK_EQ(store_text(std::string("a\tb\nc\rd\0e", 9), 40), std::string("abcde"));
    PB_CHECK_EQ(store_text("a\x01\x07\x1f" "b\x7f" "c", 40), std::string("abc"));
    PB_CHECK_EQ(store_text("a\xC2\x80\xC2\x9F" "b", 40), std::string("ab"));              // the C1 controls U+0080..U+009F
}

PB_TEST(terminal_escape_sequences_are_removed_whole) {
    PB_CHECK_EQ(store_text("red \x1b[31mtext\x1b[0m end", 40), std::string("red text end"));
    PB_CHECK_EQ(store_text("a\x1b]0;title\x07" "b", 40), std::string("ab"));              // an OSC sequence ends with BEL
    PB_CHECK_EQ(store_text("a\x1b" "b", 40), std::string("ab"));                           // a lone ESC
}

PB_TEST(direction_controls_are_removed) {
    for (const char* bad : {"\xE2\x80\xAA", "\xE2\x80\xAB", "\xE2\x80\xAC", "\xE2\x80\xAD", "\xE2\x80\xAE",       // U+202A..U+202E
                            "\xE2\x81\xA6", "\xE2\x81\xA7", "\xE2\x81\xA8", "\xE2\x81\xA9",                       // U+2066..U+2069
                            "\xE2\x80\x8E", "\xE2\x80\x8F"})                                                       // U+200E, U+200F
        PB_CHECK_EQ(store_text(std::string("ab") + bad + "cd", 40), std::string("abcd"));
}

PB_TEST(text_that_is_not_valid_utf8_does_not_get_through_unchanged) {
    const std::string out = store_text("a\xFF\xFE" "b\xC3", 40);
    PB_CHECK(out.find('\xFF') == std::string::npos);
    PB_CHECK(out.find('\xFE') == std::string::npos);
    PB_CHECK(out.front() == 'a');
}

PB_TEST(text_longer_than_the_limit_is_cut_with_an_ellipsis_counting_characters_not_bytes) {
    PB_CHECK_EQ(store_text("abcdefghij", 10), std::string("abcdefghij"));            // exactly at the limit
    PB_CHECK_EQ(store_text("abcdefghijk", 10), std::string("abcdefghi\xE2\x80\xA6")); // nine characters and the ellipsis
    const std::string cut = store_text("żżżżżżżżżżżżżżż", 5);                         // multi-byte: five characters, not five bytes
    PB_CHECK_EQ(cut, std::string("żżżż\xE2\x80\xA6"));
    PB_CHECK_EQ(store_text("abc", 1), std::string("\xE2\x80\xA6"));
    PB_CHECK_EQ(store_text("abc", 0), std::string(""));
}

PB_TEST(the_limits_are_those_of_the_catalog_contract) {
    PB_CHECK_EQ(kMaxNameChars, size_t(40));
    PB_CHECK_EQ(kMaxShortDescriptionChars, size_t(120));
    PB_CHECK_EQ(kMaxLongDescriptionChars, size_t(2000));
    PB_CHECK_EQ(kMaxAuthorChars, size_t(80));
}

PB_TEST_MAIN()
