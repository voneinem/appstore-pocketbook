// T018: the installer's rule registry E-INS-01..38 and W-INS-01..03 (contracts/rules.md) and the
// message form: what happened (with the rule id), whether the reader changed, what to do next.
#include "pbstore/install/install_rules.h"
#include "pbstore/install/messages.h"
#include "test_support.h"

#include <set>

using namespace pbstore;
using namespace pbstore::install;

PB_TEST(the_registry_holds_exactly_the_documented_ids) {
    const auto& reg = install_rule_registry();
    std::set<std::string> ids;
    for (const auto& r : reg) PB_CHECK(ids.insert(r.id).second);   // unique
    PB_CHECK_EQ(reg.size(), size_t(41));
    for (int i = 1; i <= 38; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "E-INS-%02d", i);
        PB_CHECK(ids.count(buf) == 1);
    }
    for (int i = 1; i <= 3; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "W-INS-%02d", i);
        PB_CHECK(ids.count(buf) == 1);
    }
    const InstallRule* unused = find_install_rule("E-INS-29");
    PB_CHECK(unused != nullptr);
    PB_CHECK(std::string(unused->description).find("not used") != std::string::npos);
}

PB_TEST(every_rule_has_a_description_an_effect_a_next_step_and_an_exit_status) {
    for (const auto& r : install_rule_registry()) {
        const std::string id = r.id;
        PB_CHECK(std::string(r.description).size() > 5);
        PB_CHECK(std::string(r.description).find('\n') == std::string::npos);   // one line
        PB_CHECK(r.exit_status >= 0 && r.exit_status <= 4);
        if (id != "E-INS-29" && id.compare(0, 1, "E") == 0) PB_CHECK(std::string(r.next).size() > 5);
        PB_CHECK(id.compare(0, 1, "W") != 0 || r.exit_status == 0);
    }
}

PB_TEST(effects_and_statuses_follow_the_contract_table) {
    auto st = [](const char* id) { return find_install_rule(id)->exit_status; };
    auto fx = [](const char* id) { return find_install_rule(id)->effect; };
    for (const char* id : {"E-INS-08", "E-INS-10", "E-INS-12", "E-INS-13", "E-INS-14", "E-INS-15", "E-INS-16", "E-INS-18",
                           "E-INS-19", "E-INS-20", "E-INS-21", "E-INS-22", "E-INS-24", "E-INS-28", "E-INS-30", "E-INS-31", "E-INS-35"})
        PB_CHECK_EQ(st(id), 1);
    for (const char* id : {"E-INS-01", "E-INS-02", "E-INS-03", "E-INS-04", "E-INS-05", "E-INS-06", "E-INS-07", "E-INS-25",
                           "E-INS-26", "E-INS-27", "E-INS-34", "E-INS-36", "E-INS-37"})
        PB_CHECK_EQ(st(id), 2);
    for (const char* id : {"E-INS-09", "E-INS-11", "E-INS-17", "E-INS-32", "E-INS-38"}) PB_CHECK_EQ(st(id), 2);   // a missing confirmation
    PB_CHECK_EQ(st("E-INS-33"), 3);
    PB_CHECK_EQ(st("E-INS-23"), 4);
    PB_CHECK(fx("E-INS-23") == ReaderEffect::RepairNeeded);
    PB_CHECK(fx("E-INS-24") == ReaderEffect::RepairNeeded);
    PB_CHECK(fx("E-INS-33") == ReaderEffect::Restored);
    PB_CHECK(fx("E-INS-37") == ReaderEffect::Restored);
    PB_CHECK(fx("E-INS-03") == ReaderEffect::Unchanged);
}

PB_TEST(describe_yields_the_three_parts_and_the_rule_id) {
    Finding f = make_error("E-INS-03", "/some/folder", "applications/ is missing");
    Message m = describe(f, ReaderEffect::Unchanged);
    PB_CHECK(m.what.find("E-INS-03") != std::string::npos);
    PB_CHECK(m.what.find("applications/ is missing") != std::string::npos);
    PB_CHECK(m.reader_effect.find("not changed") != std::string::npos);
    PB_CHECK(m.next.size() > 5);
    PB_CHECK_EQ(m.rule_ids, (std::vector<std::string>{"E-INS-03"}));
    const std::string t = m.text();
    PB_CHECK(t.find("E-INS-03") != std::string::npos);
    PB_CHECK(t.find("not changed") != std::string::npos);
    PB_CHECK(t.find(m.next) != std::string::npos);

    Message r = describe(make_error("E-INS-37", "view.json", "the write failed"), ReaderEffect::Restored);
    PB_CHECK(r.reader_effect.find("restored") != std::string::npos);
    Message n = describe(make_error("E-INS-23", "testapp", "an install was interrupted"), ReaderEffect::RepairNeeded);
    PB_CHECK(n.reader_effect.find("repair") != std::string::npos);
}

PB_TEST(the_effect_follows_the_registry_when_it_is_not_given) {
    Message m = describe(make_error("E-INS-23", "testapp", "interrupted"));
    PB_CHECK(m.reader_effect.find("repair") != std::string::npos);
    Message w = describe(make_notice("W-INS-01", "testapp", "already at 1.2.0"));
    PB_CHECK(w.what.find("W-INS-01") != std::string::npos);
}

PB_TEST(findings_of_the_shared_checks_keep_their_own_id) {
    for (const char* id : {"E-PKG-12", "E-CAT-01", "E-ENT-15", "E-CAT-04"}) {
        Message m = describe(make_error(id, "subject", "text of the shared check"), ReaderEffect::Unchanged);
        PB_CHECK(m.what.find(id) != std::string::npos);
        PB_CHECK_EQ(m.rule_ids, (std::vector<std::string>{id}));
        PB_CHECK(m.next.size() > 5);
        PB_CHECK_EQ(exit_status_for({make_error(id, "s", "m")}), 1);
    }
}

PB_TEST(the_exit_status_of_several_findings_is_the_most_important_one) {
    PB_CHECK_EQ(exit_status_for({}), 0);
    PB_CHECK_EQ(exit_status_for({make_notice("W-INS-03", "-", "first run")}), 0);
    PB_CHECK_EQ(exit_status_for({make_error("E-INS-12", "v", "bad"), make_error("E-INS-07", "r", "full")}), 2);   // environment first
    PB_CHECK_EQ(exit_status_for({make_error("E-INS-23", "a", "i"), make_error("E-INS-12", "v", "b")}), 4);
    PB_CHECK_EQ(exit_status_for({make_error("E-INS-33", "-", "cancelled")}), 3);
}

PB_TEST(no_message_contains_a_stack_trace_or_advice_to_use_elevated_rights) {
    for (const auto& r : install_rule_registry()) {
        for (const std::string& s : {std::string(r.description), std::string(r.next)}) {
            for (const char* bad : {"at 0x", "stack", "Traceback", "backtrace", "sudo", "root ", "chmod", "administrator"})
                PB_CHECK(s.find(bad) == std::string::npos);
        }
    }
}

PB_TEST_MAIN()
