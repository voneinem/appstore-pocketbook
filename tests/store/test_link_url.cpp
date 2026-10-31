// T183: the one address the store may hand to the reader's browser, and only after a tap and a confirmation (spec FR-011, FR-043). The check is the
// store's own (the browser is the reader's): https only, one plain line, no credentials, no more than 2048 bytes.
#include <string>

#include "pbstore/store/link_url.h"
#include "test_support.h"

using pbstore::store::is_followable_url;

PB_TEST(an_https_address_is_followable) {
    PB_CHECK(is_followable_url("https://github.com/voneinem/notes-pocketbook"));
    PB_CHECK(is_followable_url("https://github.com/voneinem/notes-pocketbook/releases?tab=1#top"));
    PB_CHECK(is_followable_url("HTTPS://GitHub.com/x"));                      // the scheme is case-insensitive
}

PB_TEST(anything_else_is_not) {
    PB_CHECK(!is_followable_url(""));
    PB_CHECK(!is_followable_url("http://github.com/x"));                      // a plain connection is not offered
    PB_CHECK(!is_followable_url("javascript:alert(1)"));
    PB_CHECK(!is_followable_url("file:///mnt/ext1/system/pbstore/app/state.json"));
    PB_CHECK(!is_followable_url("data:text/html,x"));
    PB_CHECK(!is_followable_url("github.com/x"));                             // no scheme
    PB_CHECK(!is_followable_url("https://"));                                 // no host
    PB_CHECK(!is_followable_url("https:///path"));
    PB_CHECK(!is_followable_url("https://user:pass@github.com/x"));           // credentials in the address
    PB_CHECK(!is_followable_url("https://user@github.com/x"));
    PB_CHECK(!is_followable_url("https://github.com/a b"));                   // a blank
    PB_CHECK(!is_followable_url("https://github.com/a\tb"));
    PB_CHECK(!is_followable_url("https://github.com/a\nb"));                  // a control character
    PB_CHECK(!is_followable_url(std::string("https://github.com/a") + '\0' + "b"));
    PB_CHECK(!is_followable_url("https://github.com/\x7f"));
}

PB_TEST(the_length_limit_is_2048_bytes) {
    const std::string head = "https://github.com/";
    PB_CHECK(is_followable_url(head + std::string(2048 - head.size(), 'a')));
    PB_CHECK(!is_followable_url(head + std::string(2049 - head.size(), 'a')));
}

PB_TEST_MAIN()
