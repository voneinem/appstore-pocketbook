// T038: model and firmware of the reader are read from sources that may not exist yet (research §2, F12);
// compatibility with a package is decided by a pure function.
#include "crash_fs.h"
#include "pbstore/install/identity.h"
#include "pbstore/manifest.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using pbtest::CrashFs;

namespace {
struct TestSource : IdentitySource {
    std::optional<std::string> m, f;
    std::optional<std::string> model(ReaderFs&) override { return m; }
    std::optional<std::string> firmware(ReaderFs&) override { return f; }
};
Manifest manifest(const std::string& min_fw = "6.8.0") {
    Manifest m;
    m.id = "testapp";
    m.min_firmware = min_fw;
    m.target_device = "InkPad One";
    return m;
}
bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
bool needs(const Compatibility& c, Confirmation k) {
    for (const auto& r : c.requirements) if (r.confirmation == k) return true;
    return false;
}
}  // namespace

PB_TEST(without_sources_model_and_firmware_are_unknown) {
    CrashFs fs;
    Identity id = identify(fs, {});
    PB_CHECK(!id.model.has_value());
    PB_CHECK(!id.firmware.has_value());
    PB_CHECK(default_identity_sources().empty());            // no source is known yet (task D-02)
}

PB_TEST(a_source_supplies_the_values) {
    CrashFs fs;
    TestSource s;
    s.m = "1030";
    s.f = "6.11.1437";
    Identity id = identify(fs, {&s});
    PB_CHECK_EQ(id.model.value_or(""), std::string("1030"));
    PB_CHECK_EQ(id.firmware.value_or(""), std::string("6.11.1437"));
    TestSource none;
    TestSource later;
    later.f = "6.8.4143";
    Identity second = identify(fs, {&none, &later});         // the first source that knows a value wins, per value
    PB_CHECK(!second.model.has_value());
    PB_CHECK_EQ(second.firmware.value_or(""), std::string("6.8.4143"));
}

PB_TEST(the_matching_model_and_a_new_enough_firmware_need_nothing) {
    Identity id{std::string("1030"), std::string("6.11.1437")};
    Compatibility c = check_compatibility(id, manifest("6.8.0"));
    PB_CHECK(c.refusals.empty());
    PB_CHECK(c.requirements.empty());
    Identity pb{std::string("PB1030"), std::string("6.8.0")};
    PB_CHECK(check_compatibility(pb, manifest("6.8.0")).refusals.empty());   // equal is enough
}

PB_TEST(another_readable_model_is_refused_with_e_ins_08) {
    Identity id{std::string("1040"), std::string("6.8.4143")};
    Compatibility c = check_compatibility(id, manifest());
    PB_CHECK(has_rule(c.refusals, "E-INS-08"));
    PB_CHECK(c.refusals[0].message.find("1040") != std::string::npos);
    PB_CHECK(c.refusals[0].message.find("InkPad One") != std::string::npos);
}

PB_TEST(an_unknown_model_or_firmware_needs_its_named_confirmation_both_in_one_plan) {
    Compatibility c = check_compatibility(Identity{}, manifest());
    PB_CHECK(c.refusals.empty());
    PB_CHECK_EQ(c.requirements.size(), size_t(2));
    PB_CHECK(needs(c, Confirmation::UnknownModel));
    PB_CHECK(needs(c, Confirmation::UnknownFirmware));
    bool nine = false, eleven = false;
    for (const auto& r : c.requirements) {
        if (r.finding.rule_id == "E-INS-09") nine = true;
        if (r.finding.rule_id == "E-INS-11") eleven = true;
    }
    PB_CHECK(nine && eleven);
    Compatibility only_fw = check_compatibility(Identity{std::string("1030"), std::nullopt}, manifest());
    PB_CHECK_EQ(only_fw.requirements.size(), size_t(1));
    PB_CHECK(needs(only_fw, Confirmation::UnknownFirmware));
    Compatibility garbage = check_compatibility(Identity{std::string("1030"), std::string("not a version")}, manifest());
    PB_CHECK(needs(garbage, Confirmation::UnknownFirmware));   // unreadable counts as unknown
}

PB_TEST(a_firmware_below_the_minimum_is_refused_and_both_versions_are_shown) {
    Compatibility c = check_compatibility(Identity{std::string("1030"), std::string("6.8.4143")}, manifest("6.11.0"));
    PB_CHECK(has_rule(c.refusals, "E-INS-10"));
    PB_CHECK(c.refusals[0].message.find("6.8.4143") != std::string::npos);
    PB_CHECK(c.refusals[0].message.find("6.11.0") != std::string::npos);
}

PB_TEST(firmware_versions_are_compared_as_numbers_not_text) {
    PB_CHECK(check_compatibility(Identity{std::string("1030"), std::string("6.10.0")}, manifest("6.9.0")).refusals.empty());   // 6.10 > 6.9
    PB_CHECK(!check_compatibility(Identity{std::string("1030"), std::string("6.9.0")}, manifest("6.10.0")).refusals.empty());
    PB_CHECK(check_compatibility(Identity{std::string("1030"), std::string("6.11")}, manifest("6.11.0")).refusals.empty());     // missing parts are zero
}

PB_TEST_MAIN()
