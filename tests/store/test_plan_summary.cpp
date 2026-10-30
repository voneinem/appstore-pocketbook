// What the plan page shows first: a few plain sentences an end user can follow, with no file names, no hashes, no rule ids and no
// folder lists (the owner, 2026-10-30, on the install of PB Cloud with 621 files). The installer's full text stays behind "Show details".
#include "pbstore/install/plan.h"
#include "pbstore/store/plan_summary.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::store;

namespace {

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

install::Plan cloud_plan(install::PlanKind kind = install::PlanKind::Install) {
    install::Plan p;
    p.kind = kind;
    p.app_id = "pbcloud";
    p.app_name = "PB Cloud";
    p.to_version = "0.6.1";
    p.source = "catalog";
    p.source_label = "catalog (catalog generated 2026-10-25)";
    for (int i = 0; i < 621; ++i) {
        install::PlanItem it;
        it.op = install::ItemOp::Add;
        it.path = "applications/pbcloud-zoneinfo/zone" + std::to_string(i);
        it.size = 13000;
        it.sha256 = "0123456789abcdef";
        p.items.push_back(it);
    }
    p.directories_to_create = {"applications/pbcloud-zoneinfo", "applications/pbcloud-zoneinfo/Africa"};
    p.menu.action = install::MenuChange::Action::Add;
    p.menu.id = "U_PbCloud";
    p.menu.title = "PB Cloud";
    p.menu.group = "@General";
    p.record_change = install::RecordChange::Write;
    p.warnings = {"E-INS-11: the firmware version of the reader cannot be read"};
    return p;
}

}  // namespace

PB_TEST(an_install_is_told_in_a_few_plain_sentences_without_files_folders_hashes_or_rule_ids) {
    const std::string s = plain_plan_summary(cloud_plan());
    PB_CHECK(has(s, "Install PB Cloud 0.6.1"));
    PB_CHECK(has(s, "From the PB Store catalog"));
    PB_CHECK(has(s, "MB"));
    PB_CHECK(has(s, "\"PB Cloud\" in the menu"));
    for (const char* technical : {"applications/", "sha256", "E-INS", "zone0", "Folders", "view.json", "system/pbstore", "U_PbCloud", "@General", "621"})
        PB_CHECK(!has(s, technical));
    size_t lines = 1;
    for (char c : s) if (c == '\n') ++lines;
    PB_CHECK(lines <= 6);
}

PB_TEST(an_update_names_both_versions_and_says_that_the_data_is_kept) {
    install::Plan p = cloud_plan(install::PlanKind::Update);
    p.from_version = "0.5.0";
    p.data = install::DataDirPlan{"system/pbcloud", true, "pbcloud", 11, 813 * 1024};
    const std::string s = plain_plan_summary(p);
    PB_CHECK(has(s, "Update PB Cloud from 0.5.0 to 0.6.1"));
    PB_CHECK(has(s, "Your data of this app is kept"));
    PB_CHECK(!has(s, "system/pbcloud"));
}

PB_TEST(a_removal_says_what_goes_and_that_the_data_stays_unless_asked) {
    install::Plan p = cloud_plan(install::PlanKind::Remove);
    p.menu.action = install::MenuChange::Action::Remove;
    for (auto& it : p.items) it.op = install::ItemOp::Remove;
    p.data = install::DataDirPlan{"system/pbcloud", true, "pbcloud", 11, 813 * 1024};
    const std::string s = plain_plan_summary(p);
    PB_CHECK(has(s, "Remove PB Cloud"));
    PB_CHECK(has(s, "disappears from the menu"));
    PB_CHECK(has(s, "Your data of this app is kept"));
    PB_CHECK(!has(s, "applications/"));
}

PB_TEST(an_app_from_a_local_file_carries_the_warning_about_trust) {
    install::Plan p = cloud_plan();
    p.source_label = "not from the catalog, not signed by the maintainer";
    const std::string s = plain_plan_summary(p);
    PB_CHECK(has(s, "Not from the catalog"));
    PB_CHECK(has(s, "trust"));
}

PB_TEST(replace_and_adopt_each_say_what_they_do_with_the_apps_files) {
    const std::string r = plain_plan_summary(cloud_plan(install::PlanKind::Replace));
    PB_CHECK(has(r, "Replace PB Cloud"));
    PB_CHECK(has(r, "put there by hand"));
    const std::string a = plain_plan_summary(cloud_plan(install::PlanKind::Adopt));
    PB_CHECK(has(a, "PB Cloud"));
    PB_CHECK(has(a, "No files change"));
}

PB_TEST(files_the_user_changed_since_the_install_are_counted_not_listed) {
    install::Plan p = cloud_plan(install::PlanKind::Update);
    p.from_version = "0.5.0";
    p.items[0].reason = "changed since the install, replaced";
    p.items[1].reason = "changed since the install, replaced";
    const std::string s = plain_plan_summary(p);
    PB_CHECK(has(s, "2 files of this app were changed since it was installed"));
    PB_CHECK(!has(s, "zone0"));
}

PB_TEST(untrusted_names_lose_control_characters_and_are_cut) {
    install::Plan p = cloud_plan();
    p.app_name = "Evil\nLine\x1b[31m" + std::string(100, 'x');
    p.menu.title = p.app_name;
    const std::string s = plain_plan_summary(p);
    PB_CHECK(!has(s, "\x1b"));
    PB_CHECK(!has(s, "Evil\nLine"));
    PB_CHECK(s.size() < 600);
}

PB_TEST(the_deletion_of_a_data_folder_names_the_folder_and_the_number_of_files_the_consents_refer_to) {
    install::Plan p;
    p.kind = install::PlanKind::DeleteData;
    p.app_id = "pbcloud";
    p.app_name = "PB Cloud";
    p.data = install::DataDirPlan{"system/pbcloud", true, "", 11, 813 * 1024};
    const std::string s = plain_plan_summary(p);
    PB_CHECK(has(s, "system/pbcloud"));
    PB_CHECK(has(s, "11 files"));
    PB_CHECK(has(s, "cannot be undone"));
}

PB_TEST_MAIN()
