// T066: the search (FR-010) and the order of the list (FR-009).
#include "pbstore/store/app_filter.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {
AppRow row(const std::string& id, const std::string& name, RowState state = RowState::NotInstalled) {
    AppRow r;
    r.id = id;
    r.name = name;
    r.state = state;
    return r;
}
std::vector<std::string> ids(const std::vector<AppRow>& rows) {
    std::vector<std::string> v;
    for (const auto& r : rows) v.push_back(r.id);
    return v;
}
}  // namespace

PB_TEST(the_filter_keeps_apps_whose_name_contains_the_text_ignoring_ascii_case) {
    std::vector<AppRow> rows = {row("a", "PB Notes"), row("b", "PB Cloud"), row("c", "Reader Tools"), row("d", "notebook")};
    PB_CHECK((ids(filter_apps(rows, "note")) == std::vector<std::string>{"a", "d"}));
    PB_CHECK((ids(filter_apps(rows, "NOTE")) == std::vector<std::string>{"a", "d"}));
    PB_CHECK((ids(filter_apps(rows, "pb ")) == std::vector<std::string>{"a", "b"}));
    PB_CHECK((ids(filter_apps(rows, "ools")) == std::vector<std::string>{"c"}));
}

PB_TEST(empty_text_returns_everything_and_no_match_returns_nothing) {
    std::vector<AppRow> rows = {row("a", "PB Notes"), row("b", "PB Cloud")};
    PB_CHECK((ids(filter_apps(rows, "")) == std::vector<std::string>{"a", "b"}));
    PB_CHECK(filter_apps(rows, "zzz").empty());
    PB_CHECK(filter_apps({}, "x").empty());
}

PB_TEST(the_order_of_the_input_is_preserved) {
    std::vector<AppRow> rows = {row("z", "Zed Note"), row("a", "A Note"), row("m", "Mid Note")};
    PB_CHECK((ids(filter_apps(rows, "note")) == std::vector<std::string>{"z", "a", "m"}));
}

PB_TEST(only_the_name_is_searched_not_the_id_or_the_description) {
    AppRow r = row("secret-id", "Visible");
    r.short_description = "contains needle";
    PB_CHECK(filter_apps({r}, "secret").empty());
    PB_CHECK(filter_apps({r}, "needle").empty());
    PB_CHECK(filter_apps({r}, "visi").size() == 1);
}

PB_TEST(the_unfiltered_order_is_catalog_apps_by_name_ignoring_case_then_apps_not_in_the_catalog_by_name) {
    std::vector<AppRow> rows = {row("u2", "zeta hand", RowState::NotInCatalog), row("c2", "beta", RowState::Installed),
                                row("u1", "Alpha hand", RowState::NotInCatalog), row("c3", "Gamma", RowState::NotInstalled),
                                row("c1", "Alpha", RowState::UpdateAvailable)};
    sort_rows(&rows);
    PB_CHECK((ids(rows) == std::vector<std::string>{"c1", "c2", "c3", "u1", "u2"}));
}

PB_TEST(apps_with_an_update_come_first_by_name_then_the_other_catalog_apps_then_the_apps_not_in_the_catalog) {
    // feature 004, FR-013: the group key is applied before the name; inside a group the name is folded as before
    std::vector<AppRow> rows = {row("z", "Zeta", RowState::UpdateAvailable), row("a", "Alpha", RowState::Installed),
                                row("b", "beta", RowState::UpdateAvailable), row("g", "Gamma", RowState::NotInstalled),
                                row("m", "Mike", RowState::InstalledOutside), row("h", "Aaa hand", RowState::NotInCatalog)};
    sort_rows(&rows);
    PB_CHECK((ids(rows) == std::vector<std::string>{"b", "z", "a", "g", "m", "h"}));
}

PB_TEST(a_search_keeps_the_grouping_of_the_sorted_list) {
    std::vector<AppRow> rows = {row("z", "Zeta app", RowState::UpdateAvailable), row("a", "Alpha app", RowState::Installed),
                                row("b", "beta app", RowState::UpdateAvailable), row("x", "Other", RowState::UpdateAvailable)};
    sort_rows(&rows);
    PB_CHECK((ids(filter_apps(rows, "app")) == std::vector<std::string>{"b", "z", "a"}));   // updatable matches first, then the others
}

PB_TEST(only_ascii_letters_are_folded_other_characters_must_match_exactly) {
    std::vector<AppRow> rows = {row("a", "\xC5\xBB\xC3\xB3\xC5\x82w")};   // "Żółw"
    PB_CHECK(filter_apps(rows, "\xC5\xBB\xC3\xB3").size() == 1);           // "Żó": the same bytes
    PB_CHECK(filter_apps(rows, "\xC5\xBC\xC3\xB3").empty());               // "żó": Ż and ż are not folded, only ASCII is
    PB_CHECK(filter_apps(rows, "W").size() == 1);                              // the ASCII letter w is
}

PB_TEST_MAIN()
