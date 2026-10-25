// T033: the desktop configuration editor (system/config/desktop/view.json) changes only one app's entry,
// as text splices, so every other byte stays as it was (contracts/reader-layout.md, spec FR-014, FR-015).
#include "fake_reader.h"
#include "pbstore/install/desktop_config.h"
#include "pbstore/json.h"
#include "test_support.h"

#include <fstream>
#include <sstream>

using namespace pbstore;
using namespace pbstore::install;

namespace {
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

EntryValue test_value() {
    return {"/mnt/ext1/applications/testapp.app", "Test App", "applications/icons/testapp.bmp", "applications/icons/testapp_f.bmp"};
}

DesktopConfig must_parse(const std::string& text) {
    ParseResult p = parse_desktop_config(text);
    if (!p.config) PB_FAIL_AT("parse failed: " + (p.findings.empty() ? std::string("?") : p.findings[0].message));
    return *p.config;
}

std::string text_of(pbtest::FakeReader::Style s) { return pbtest::FakeReader(s).files["system/config/desktop/view.json"]; }

// the old and new text with the changed spans cut out must be identical
bool same_outside(const std::string& old_text, const EditResult& r) {
    std::string a = old_text, b = *r.text;
    // where each splice's text starts in the new file
    std::vector<size_t> new_begin;
    long shift = 0;
    for (const Splice& s : r.splices) {
        new_begin.push_back(static_cast<size_t>(static_cast<long>(s.begin) + shift));
        shift += static_cast<long>(s.text.size()) - static_cast<long>(s.end - s.begin);
    }
    // cut from the back so that offsets stay valid
    for (size_t i = r.splices.size(); i-- > 0;) {
        a.erase(r.splices[i].begin, r.splices[i].end - r.splices[i].begin);
        b.erase(new_begin[i], r.splices[i].text.size());
    }
    return a == b;
}
}  // namespace

PB_TEST(the_shape_check_accepts_the_observed_form) {
    for (auto style : {pbtest::FakeReader::Pretty2, pbtest::FakeReader::Pretty4, pbtest::FakeReader::Tabs, pbtest::FakeReader::Minified}) {
        ParseResult p = parse_desktop_config(text_of(style));
        PB_CHECK(p.config.has_value());
        PB_CHECK(p.findings.empty());
        PB_CHECK(p.config->has_entry("U_AlphaApp"));
        PB_CHECK(!p.config->has_entry("U_TestApp"));
        PB_CHECK(p.config->has_group("@General"));
        PB_CHECK(p.config->has_group("@Favorites"));
        PB_CHECK_EQ(p.config->sha256.size(), size_t(64));
    }
}

// The file as read from a real InkPad One (firmware 6.11.1437, 2026-10-25): the groups are under "view", and both
// "applications" and "view" carry a "_comment" key that is documentation, not an entry.
std::string observed_view() {
    std::ifstream f(std::string(PBSTORE_SOURCE_DIR) + "/tests/fixtures/view-inkpad-one-observed.json", std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

PB_TEST(the_file_of_a_real_reader_is_accepted_with_its_comments_and_groups_under_view) {
    const std::string text = observed_view();
    PB_CHECK(!text.empty());
    ParseResult p = parse_desktop_config(text);
    PB_CHECK(p.config.has_value());
    PB_CHECK(p.findings.empty());
    PB_CHECK(p.config->has_entry("U_PbNotes"));
    PB_CHECK(p.config->has_entry("U_PbCloud"));
    PB_CHECK(!p.config->has_entry("U_TestApp"));
    PB_CHECK(p.config->has_group("@General"));
    PB_CHECK(p.config->has_group("@Services"));
    PB_CHECK(p.config->has_group("@Games"));
}

PB_TEST(an_entry_is_added_to_and_removed_from_the_file_of_a_real_reader_changing_only_its_own_spans) {
    const std::string text = observed_view();
    DesktopConfig cfg = must_parse(text);
    EditResult added = edit(cfg, add_entry("U_TestApp", test_value(), "@General"));
    PB_CHECK(added.findings.empty());
    PB_CHECK(added.text.has_value());
    PB_CHECK(same_outside(text, added));
    DesktopConfig after = must_parse(*added.text);
    PB_CHECK(after.has_entry("U_TestApp"));
    PB_CHECK(after.has_entry("U_PbNotes"));
    PB_CHECK(has(*added.text, "\"_comment\""));
    // the id is last in @General, which also holds the other U_ ids
    PB_CHECK(has(*added.text, "\"U_PbOffice\",\n                    \"U_TestApp\""));   // same indentation as its neighbours
    EditResult gone = edit(after, remove_entry("U_TestApp"));
    PB_CHECK(gone.findings.empty());
    PB_CHECK_EQ(*gone.text, text);   // the round trip restores every byte
}

PB_TEST(a_group_without_an_apps_list_is_accepted_but_cannot_take_an_entry) {
    const std::string text = "{\"applications\":{},\"view\":{\"groups\":[{\"title\":\"@Empty\"},{\"title\":\"@General\",\"apps\":[]}]}}";
    DesktopConfig cfg = must_parse(text);
    PB_CHECK(cfg.has_group("@Empty"));
    EditResult r = edit(cfg, add_entry("U_TestApp", test_value(), "@Empty"));
    PB_CHECK(!r.text.has_value());
    PB_CHECK_EQ(r.findings[0].rule_id, std::string("E-INS-13"));
    EditResult ok = edit(cfg, add_entry("U_TestApp", test_value(), "@General"));
    PB_CHECK(ok.text.has_value());
    PB_CHECK(must_parse(*ok.text).has_entry("U_TestApp"));
}

PB_TEST(text_that_is_not_valid_json_is_refused_with_e_ins_12) {
    const std::string good = text_of(pbtest::FakeReader::Pretty2);
    std::string deep(40, '[');
    deep += std::string(40, ']');
    for (const std::string& bad : {std::string(""), std::string("not json"), good.substr(0, good.size() / 2), good + " trailing",
                                   std::string("{\"applications\":{},\"applications\":{},\"groups\":[]}"), deep}) {
        ParseResult p = parse_desktop_config(bad);
        PB_CHECK(!p.config.has_value());
        PB_CHECK(!p.findings.empty());
        PB_CHECK_EQ(p.findings[0].rule_id, std::string("E-INS-12"));
        PB_CHECK(!p.findings[0].message.empty());
    }
}

PB_TEST(a_file_without_the_expected_parts_is_refused_with_e_ins_13_naming_the_part) {
    struct Case { const char* text; const char* names; };
    const Case cases[] = {
        {"{\"view\":{\"groups\":[]}}", "applications"},
        {"{\"applications\":{}}", "view"},
        {"{\"applications\":{},\"view\":{}}", "groups"},
        {"{\"applications\":{},\"groups\":[]}", "view"},     // the groups are inside "view", not at the top
        {"{\"applications\":{},\"view\":[]}", "view"},
        {"[]", "object"},
        {"{\"applications\":[],\"view\":{\"groups\":[]}}", "applications"},
        {"{\"applications\":{\"U_A\":5},\"view\":{\"groups\":[]}}", "applications"},
        {"{\"applications\":{},\"view\":{\"groups\":{}}}", "groups"},
        {"{\"applications\":{},\"view\":{\"groups\":[{\"apps\":[]}]}}", "title"},
        {"{\"applications\":{},\"view\":{\"groups\":[{\"title\":\"G\",\"apps\":5}]}}", "apps"},
        {"{\"applications\":{},\"view\":{\"groups\":[{\"title\":\"G\",\"apps\":[1]}]}}", "apps"},
        {"{\"applications\":{},\"view\":{\"groups\":[{\"title\":5,\"apps\":[]}]}}", "title"},
        {"{\"applications\":{},\"view\":{\"groups\":[5]}}", "groups"},
    };
    for (const Case& c : cases) {
        ParseResult p = parse_desktop_config(c.text);
        PB_CHECK(!p.config.has_value());
        PB_CHECK(!p.findings.empty());
        PB_CHECK_EQ(p.findings[0].rule_id, std::string("E-INS-13"));
        PB_CHECK(has(p.findings[0].message, c.names));
    }
}

PB_TEST(adding_an_entry_appends_the_member_and_the_id_to_the_named_group) {
    const std::string old_text = text_of(pbtest::FakeReader::Pretty2);
    DesktopConfig c = must_parse(old_text);
    EditResult r = edit(c, add_entry("U_TestApp", test_value(), "@General"));
    PB_CHECK(r.text.has_value());
    PB_CHECK(r.findings.empty());
    ParseResult after = parse_desktop_config(*r.text);
    PB_CHECK(after.config.has_value());
    PB_CHECK(after.config->has_entry("U_TestApp"));
    // the new member is the last of "applications", its id the last of the group's "apps"
    const json::Value* apps = after.config->tree.find("applications");
    PB_CHECK_EQ(apps->object.back().first, std::string("U_TestApp"));
    PB_CHECK_EQ(apps->object.back().second.get_string("path").value_or(""), std::string("/mnt/ext1/applications/testapp.app"));
    PB_CHECK_EQ(apps->object.back().second.get_string("title").value_or(""), std::string("Test App"));
    PB_CHECK_EQ(apps->object.back().second.find("icon")->get_string("path").value_or(""), std::string("applications/icons/testapp.bmp"));
    PB_CHECK_EQ(apps->object.back().second.find("focused_icon")->get_string("path").value_or(""), std::string("applications/icons/testapp_f.bmp"));
    const json::Value* groups = after.config->tree.find("view")->find("groups");
    PB_CHECK_EQ(groups->array[0].find("apps")->array.back().string, std::string("U_TestApp"));
    PB_CHECK(groups->array[1].find("apps")->array.empty() || groups->array[1].find("apps")->array.back().string != "U_TestApp");
    PB_CHECK(same_outside(old_text, r));
    PB_CHECK(verify(old_text, *r.text, add_entry("U_TestApp", test_value(), "@General")));
}

PB_TEST(a_second_group_is_chosen_by_its_title) {
    const std::string old_text = text_of(pbtest::FakeReader::Pretty2);
    DesktopConfig c = must_parse(old_text);
    EditResult r = edit(c, add_entry("U_TestApp", test_value(), "@Favorites"));
    PB_CHECK(r.text.has_value());
    ParseResult after = parse_desktop_config(*r.text);
    const json::Value* groups = after.config->tree.find("view")->find("groups");
    bool in_favorites = false, in_general = false;
    for (const auto& g : groups->array)
        for (const auto& a : g.find("apps")->array)
            if (a.string == "U_TestApp") (g.get_string("title") == std::string("@Favorites") ? in_favorites : in_general) = true;
    PB_CHECK(in_favorites);
    PB_CHECK(!in_general);
    PB_CHECK(same_outside(old_text, r));
}

PB_TEST(a_missing_group_gives_e_ins_14_lists_the_groups_and_changes_nothing) {
    DesktopConfig c = must_parse(text_of(pbtest::FakeReader::Pretty2));
    EditResult r = edit(c, add_entry("U_TestApp", test_value(), "No Such Group"));
    PB_CHECK(!r.text.has_value());
    PB_CHECK(r.splices.empty());
    PB_CHECK(!r.findings.empty());
    PB_CHECK_EQ(r.findings[0].rule_id, std::string("E-INS-14"));
    PB_CHECK(has(r.findings[0].message, "@General"));
    PB_CHECK(has(r.findings[0].message, "@Favorites"));
    PB_CHECK(has(r.findings[0].message, "No Such Group"));
}

PB_TEST(an_id_that_already_exists_cannot_be_added) {
    DesktopConfig c = must_parse(text_of(pbtest::FakeReader::Pretty2));
    EditResult r = edit(c, add_entry("U_AlphaApp", test_value(), "@General"));
    PB_CHECK(!r.text.has_value());
    PB_CHECK_EQ(r.findings[0].rule_id, std::string("E-INS-15"));
}

PB_TEST(replacing_an_entry_changes_only_that_value) {
    const std::string old_text = text_of(pbtest::FakeReader::Pretty2);
    DesktopConfig c = must_parse(old_text);
    EntryValue v = {"/mnt/ext1/applications/alpha2.app", "Alpha Two", "applications/icons/a2.bmp", "applications/icons/a2_f.bmp"};
    EditResult r = edit(c, replace_entry("U_AlphaApp", v, "@General"));
    PB_CHECK(r.text.has_value());
    PB_CHECK_EQ(r.splices.size(), size_t(1));                // the group did not change: one splice
    PB_CHECK(has(*r.text, "Alpha Two"));
    PB_CHECK(!has(*r.text, "\"title\": \"Alpha\""));
    PB_CHECK(same_outside(old_text, r));
    ParseResult after = parse_desktop_config(*r.text);
    PB_CHECK(after.config.has_value());
    // an identical value changes nothing at all
    const json::Value* cur = c.tree.find("applications")->find("U_AlphaApp");
    EntryValue same = {cur->get_string("path").value(), cur->get_string("title").value(), cur->find("icon")->get_string("path").value(),
                       cur->find("focused_icon")->get_string("path").value()};
    EditResult n = edit(c, replace_entry("U_AlphaApp", same, "@General"));
    PB_CHECK(n.text.has_value());
    PB_CHECK_EQ(*n.text, old_text);
    PB_CHECK(n.splices.empty());
}

PB_TEST(replacing_moves_the_id_to_another_group_only_when_the_group_changed) {
    const std::string old_text = text_of(pbtest::FakeReader::Pretty2);
    DesktopConfig c = must_parse(old_text);
    const json::Value* cur = c.tree.find("applications")->find("U_AlphaApp");
    EntryValue same = {cur->get_string("path").value(), cur->get_string("title").value(), cur->find("icon")->get_string("path").value(),
                       cur->find("focused_icon")->get_string("path").value()};
    EditResult r = edit(c, replace_entry("U_AlphaApp", same, "@Favorites"));
    PB_CHECK(r.text.has_value());
    ParseResult after = parse_desktop_config(*r.text);
    const json::Value* groups = after.config->tree.find("view")->find("groups");
    int occurrences = 0;
    std::string where;
    for (const auto& g : groups->array)
        for (const auto& a : g.find("apps")->array)
            if (a.string == "U_AlphaApp") { ++occurrences; where = g.get_string("title").value(); }
    PB_CHECK_EQ(occurrences, 1);
    PB_CHECK_EQ(where, std::string("@Favorites"));
    PB_CHECK(same_outside(old_text, r));
    EditResult missing = edit(c, replace_entry("U_AlphaApp", same, "Nowhere"));
    PB_CHECK(!missing.text.has_value());
    PB_CHECK_EQ(missing.findings[0].rule_id, std::string("E-INS-14"));
}

PB_TEST(removing_an_entry_deletes_the_member_and_every_occurrence_of_the_id) {
    const std::string old_text = text_of(pbtest::FakeReader::Pretty2);
    DesktopConfig c = must_parse(old_text);
    for (const char* id : {"U_AlphaApp", "U_BetaApp", "U_GammaApp", "Dictionary"}) {   // first, middle, last, a vendor entry
        if (!c.has_entry(id)) continue;
        EditResult r = edit(c, remove_entry(id));
        PB_CHECK(r.text.has_value());
        ParseResult after = parse_desktop_config(*r.text);
        PB_CHECK(after.config.has_value());
        PB_CHECK(!after.config->has_entry(id));
        for (const auto& g : after.config->tree.find("view")->find("groups")->array)
            for (const auto& a : g.find("apps")->array) PB_CHECK(a.string != id);
        PB_CHECK_EQ(after.config->tree.find("applications")->object.size(), c.tree.find("applications")->object.size() - 1);
        PB_CHECK(same_outside(old_text, r));
    }
}

PB_TEST(removing_the_only_member_and_the_only_id_leaves_valid_empty_containers) {
    const std::string old_text = "{\n  \"applications\": {\n    \"U_Only\": { \"path\": \"/mnt/ext1/applications/o.app\", \"title\": \"O\", \"icon\": {\"path\": \"i\"}, \"focused_icon\": {\"path\": \"f\"} }\n  },\n  \"view\": { \"groups\": [\n    { \"title\": \"G\", \"apps\": [ \"U_Only\" ] }\n  ] }\n}\n";
    DesktopConfig c = must_parse(old_text);
    EditResult r = edit(c, remove_entry("U_Only"));
    PB_CHECK(r.text.has_value());
    ParseResult after = parse_desktop_config(*r.text);
    PB_CHECK(after.config.has_value());
    PB_CHECK(after.config->tree.find("applications")->object.empty());
    PB_CHECK(after.config->tree.find("view")->find("groups")->array[0].find("apps")->array.empty());
    // and adding into the now empty containers works
    EditResult again = edit(*after.config, add_entry("U_New", test_value(), "G"));
    PB_CHECK(again.text.has_value());
    PB_CHECK(parse_desktop_config(*again.text).config.has_value());
}

PB_TEST(removing_an_unknown_id_is_not_an_edit) {
    DesktopConfig c = must_parse(text_of(pbtest::FakeReader::Pretty2));
    EditResult r = edit(c, remove_entry("U_NoSuchApp"));
    PB_CHECK(r.text.has_value());
    PB_CHECK_EQ(*r.text, c.text);
    PB_CHECK(r.splices.empty());
}

PB_TEST(new_text_follows_the_indentation_of_the_neighbouring_members) {
    {   // two spaces
        DesktopConfig c = must_parse(text_of(pbtest::FakeReader::Pretty2));
        std::string t = *edit(c, add_entry("U_TestApp", test_value(), "@General")).text;
        PB_CHECK(has(t, ",\n    \"U_TestApp\": {\n      \"path\": \"/mnt/ext1/applications/testapp.app\","));
    }
    {   // four spaces
        DesktopConfig c = must_parse(text_of(pbtest::FakeReader::Pretty4));
        std::string t = *edit(c, add_entry("U_TestApp", test_value(), "@General")).text;
        PB_CHECK(has(t, ",\n        \"U_TestApp\": {\n            \"path\": "));
    }
    {   // tabs
        DesktopConfig c = must_parse(text_of(pbtest::FakeReader::Tabs));
        std::string t = *edit(c, add_entry("U_TestApp", test_value(), "@General")).text;
        PB_CHECK(has(t, ",\n\t\t\"U_TestApp\": {\n\t\t\t\"path\": "));
        PB_CHECK(!has(t, "  \"U_TestApp\""));
    }
    {   // no line breaks: compact output
        DesktopConfig c = must_parse(text_of(pbtest::FakeReader::Minified));
        std::string t = *edit(c, add_entry("U_TestApp", test_value(), "@General")).text;
        PB_CHECK(t.find('\n') == std::string::npos);
        PB_CHECK(has(t, ",\"U_TestApp\":{\"path\":\"/mnt/ext1/applications/testapp.app\""));
    }
}

PB_TEST(unknown_keys_other_apps_other_groups_and_vendor_entries_stay_byte_for_byte) {
    for (auto style : {pbtest::FakeReader::Pretty2, pbtest::FakeReader::Tabs, pbtest::FakeReader::Minified}) {
        const std::string old_text = text_of(style);
        DesktopConfig c = must_parse(old_text);
        EditResult r = edit(c, add_entry("U_TestApp", test_value(), "@General"));
        PB_CHECK(r.text.has_value());
        PB_CHECK(same_outside(old_text, r));
        PB_CHECK(r.splices.size() == 2);                     // the member and the id in the group, nothing else
        // the whole old text is the new text with those two insertions removed
        PB_CHECK(verify(old_text, *r.text, add_entry("U_TestApp", test_value(), "@General")));
    }
}

PB_TEST(verify_rejects_a_text_that_does_more_than_the_change) {
    const std::string old_text = text_of(pbtest::FakeReader::Pretty2);
    DesktopConfig c = must_parse(old_text);
    Change add = add_entry("U_TestApp", test_value(), "@General");
    std::string good = *edit(c, add).text;
    PB_CHECK(verify(old_text, good, add));
    // another entry's title changed as well
    std::string two = good;
    const size_t at = two.find("\"title\": \"Alpha\"");
    PB_CHECK(at != std::string::npos);
    two.replace(at, 16, "\"title\": \"Omega\"");
    std::string why;
    PB_CHECK(!verify(old_text, two, add, &why));
    PB_CHECK(!why.empty());
    // the id was added to the member list but not to the group
    PB_CHECK(!verify(old_text, old_text, add));
    // text that does not parse
    PB_CHECK(!verify(old_text, good.substr(0, good.size() - 5), add));
    // whitespace changed elsewhere (a reformatting)
    std::string reformatted = good;
    reformatted.insert(reformatted.find("\"groups\""), " ");
    PB_CHECK(!verify(old_text, reformatted, add));
}

PB_TEST(the_entry_value_comes_from_the_manifest_with_the_device_root_prefix) {
    MenuEntry m;
    m.id = "U_TestApp";
    m.title = "Test App";
    m.path = "applications/testapp.app";
    m.icon = "applications/icons/testapp.bmp";
    m.focused_icon = "applications/icons/testapp_f.bmp";
    m.group = "@General";
    EntryValue v = entry_value_from_manifest(m);
    PB_CHECK_EQ(v.path, std::string("/mnt/ext1/applications/testapp.app"));
    PB_CHECK_EQ(v.title, std::string("Test App"));
    PB_CHECK_EQ(v.icon_path, std::string("applications/icons/testapp.bmp"));       // relative, unchanged
    PB_CHECK_EQ(v.focused_icon_path, std::string("applications/icons/testapp_f.bmp"));
    PB_CHECK_EQ(std::string(kDeviceRootPrefix), std::string("/mnt/ext1/"));
}

PB_TEST(titles_and_ids_with_special_characters_are_escaped) {
    DesktopConfig c = must_parse(text_of(pbtest::FakeReader::Pretty2));
    EntryValue v = test_value();
    v.title = "Quote \" Back\\slash \xc3\xa4";
    EditResult r = edit(c, add_entry("U_TestApp", v, "@General"));
    PB_CHECK(r.text.has_value());
    ParseResult after = parse_desktop_config(*r.text);
    PB_CHECK(after.config.has_value());
    PB_CHECK_EQ(after.config->tree.find("applications")->find("U_TestApp")->get_string("title").value(), v.title);
}

PB_TEST_MAIN()
