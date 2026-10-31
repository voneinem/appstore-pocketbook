// T176: the last message of a finished action. It says where the app appears only when the plan changes the menu entry (an update of PB Store
// leaves it as it is: the text used to say `appears in the group "" as ""`), and that the data folder was kept only when there is one.
#include "pbstore/install/messages.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

Plan update_plan(bool menu, bool data_folder) {
    Plan p;
    p.kind = PlanKind::Update;
    p.app_id = "demo";
    p.app_name = "Demo";
    p.from_version = "1.0.0";
    p.to_version = "1.1.0";
    if (menu) {
        p.menu.action = MenuChange::Action::Replace;
        p.menu.group = "Applications";
        p.menu.title = "Demo App";
    }
    if (data_folder) {
        p.data.emplace();
        p.data->path = "system/demo";
        p.data->exists = true;
    }
    return p;
}
}  // namespace

PB_TEST(an_update_that_leaves_the_menu_entry_says_nothing_about_the_menu) {
    const std::string t = completion_text(update_plan(false, false));
    PB_CHECK(has(t, "Done. Demo is now version 1.1.0 (it was 1.0.0)."));
    PB_CHECK(!has(t, "appears"));
    PB_CHECK(!has(t, "\"\""));
    PB_CHECK(!has(t, "data folder"));                       // there is none
}

PB_TEST(an_update_with_a_menu_entry_and_a_data_folder_says_both) {
    const std::string t = completion_text(update_plan(true, true));
    PB_CHECK(has(t, "It appears in the group \"Applications\" as \"Demo App\"."));
    PB_CHECK(has(t, "Its data folder was kept."));
}

PB_TEST(an_install_without_a_menu_entry_and_one_without_a_group_are_worded_plainly) {
    Plan p = update_plan(false, false);
    p.kind = PlanKind::Install;
    PB_CHECK(has(completion_text(p), "Done. Demo 1.1.0 is installed."));
    PB_CHECK(!has(completion_text(p), "appears"));
    p.menu.action = MenuChange::Action::Add;
    p.menu.title = "Demo App";
    PB_CHECK(has(completion_text(p), "It appears as \"Demo App\"."));
}

PB_TEST_MAIN()
