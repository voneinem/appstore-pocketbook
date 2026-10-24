// T016: rule registry and finding formatting.
#include "pbstore/rules.h"
#include "test_support.h"

#include <set>

using namespace pbstore;

static std::set<std::string> expected_ids() {
    std::set<std::string> ids;
    auto add = [&](const char* prefix, int last) {
        for (int i = 1; i <= last; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%s-%02d", prefix, i);
            ids.insert(buf);
        }
    };
    add("E-ENT", 17);
    add("E-PKG", 15);
    add("E-CAT", 5);
    add("E-BLD", 7);
    add("W-ENT", 2);
    add("W-PR", 1);
    return ids;
}

PB_TEST(registry_has_exactly_the_documented_ids) {
    std::set<std::string> seen;
    for (const RuleInfo& r : rule_registry()) {
        PB_CHECK(seen.insert(r.id).second);                       // unique
        PB_CHECK(std::string(r.description).size() > 5);          // non-empty one-liner
        PB_CHECK(std::string(r.description).find('\n') == std::string::npos);
        PB_CHECK(is_known_rule(r.id));
    }
    PB_CHECK(seen == expected_ids());
    PB_CHECK(!is_known_rule("E-ENT-18"));
    PB_CHECK(!is_known_rule(""));
}

PB_TEST(errors_and_notices_are_formatted_as_documented) {
    Finding e = make_error("E-PKG-12", "applications/x.bin", "SHA-256 differs");
    PB_CHECK_EQ(format_finding(e), std::string("error[E-PKG-12] applications/x.bin: SHA-256 differs"));
    Finding n = make_notice("W-ENT-01", "sample-app", "from a/b to c/d");
    PB_CHECK_EQ(format_finding(n), std::string("notice[W-ENT-01] sample-app: from a/b to c/d"));
    PB_CHECK(e.severity == Severity::Error);
    PB_CHECK(n.severity == Severity::Notice);
}

PB_TEST(github_annotations_escape_special_characters) {
    Finding e = make_error("E-ENT-06", "sample-app", "license \"X\" 100%\nbad: yes\r!");
    const std::string a = github_annotation(e, "catalog/entries/sample-app.json");
    PB_CHECK_EQ(a.substr(0, 8), std::string("::error "));
    PB_CHECK(a.find("file=catalog/entries/sample-app.json::") != std::string::npos);
    PB_CHECK(a.find("%25") != std::string::npos);   // %
    PB_CHECK(a.find("%0A") != std::string::npos);   // \n
    PB_CHECK(a.find("%0D") != std::string::npos);   // \r
    PB_CHECK(a.find('\n') == std::string::npos);
    PB_CHECK(a.find('\r') == std::string::npos);
    PB_CHECK(a.find("[E-ENT-06] sample-app: license") != std::string::npos);
    // colons and commas in property values are escaped, in the message they are not
    const std::string b = github_annotation(e, "a:b,c.json");
    PB_CHECK(b.find("file=a%3Ab%2Cc.json::") != std::string::npos);
    PB_CHECK(b.find("bad: yes") != std::string::npos);
    PB_CHECK_EQ(github_annotation(e, "").substr(0, 9), std::string("::error::"));
    Finding n = make_notice("W-ENT-02", "sample-app", "looks like an e-mail address");
    PB_CHECK_EQ(github_annotation(n, "").substr(0, 10), std::string("::notice::"));
}

PB_TEST_MAIN()
