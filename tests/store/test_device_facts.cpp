// T038: what the result texts say depends on two device facts that are not verified yet (restart needed, shown without a
// restart). One table, a test per combination; the default is "all unverified" and says "may" (contracts/ui-pages.md,
// research R-11). Adopt never talks about a restart because it only writes a record; the self-update text is about
// opening PB Store again, never about restarting the reader.
#include <algorithm>

#include "pbstore/store/device_facts.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

WordingArgs args() {
    WordingArgs a;
    a.app = "PB Notes";
    a.group = "General";
    a.folder = "system/pbnotes";
    a.version = "0.2.0";
    a.repair_kind = "update";
    a.repair_finished = true;
    return a;
}

const Fact kFacts[] = {Fact::Unverified, Fact::Yes, Fact::No};

}  // namespace

PB_TEST(the_default_is_all_unverified) {
    DeviceFacts f;
    PB_CHECK(f.restart_needed == Fact::Unverified);
    PB_CHECK(f.shown_without_restart == Fact::Unverified);
    PB_CHECK(restart_state(f) == RestartState::Maybe);
}

PB_TEST(the_nine_combinations_give_the_documented_restart_state) {
    //                    restart_needed        shown_without_restart
    struct Row { Fact needed, shown; RestartState expect; };
    const Row rows[] = {
        {Fact::Unverified, Fact::Unverified, RestartState::Maybe},
        {Fact::Unverified, Fact::Yes, RestartState::NotNeeded},
        {Fact::Unverified, Fact::No, RestartState::Needed},
        {Fact::Yes, Fact::Unverified, RestartState::Needed},
        {Fact::Yes, Fact::Yes, RestartState::Needed},          // contradictory: the safe wording wins
        {Fact::Yes, Fact::No, RestartState::Needed},
        {Fact::No, Fact::Unverified, RestartState::NotNeeded},
        {Fact::No, Fact::Yes, RestartState::NotNeeded},
        {Fact::No, Fact::No, RestartState::NotNeeded},         // contradictory: the explicit "not needed" wins
    };
    for (const Row& r : rows) {
        DeviceFacts f;
        f.restart_needed = r.needed;
        f.shown_without_restart = r.shown;
        PB_CHECK(restart_state(f) == r.expect);
    }
}

PB_TEST(install_and_update_texts_follow_the_table_for_the_default) {
    DeviceFacts f;
    const std::string expected =
        "PB Notes is installed. It will appear in General. If it is not there, restart the reader. "
        "The next start of the reader may take noticeably longer; wait for it.";
    PB_CHECK_EQ(result_text(f, WordingOp::Install, args()), expected);
    // an update also says that the app's data was kept (spec FR-018)
    PB_CHECK_EQ(result_text(f, WordingOp::Update, args()),
                "PB Notes is installed. It will appear in General. Its data was kept. If it is not there, restart the reader. "
                "The next start of the reader may take noticeably longer; wait for it.");
}

PB_TEST(install_texts_for_every_combination) {
    for (Fact needed : kFacts)
        for (Fact shown : kFacts) {
            DeviceFacts f;
            f.restart_needed = needed;
            f.shown_without_restart = shown;
            const std::string t = result_text(f, WordingOp::Install, args());
            PB_CHECK(has(t, "PB Notes is installed."));
            switch (restart_state(f)) {
                case RestartState::Maybe:
                    PB_CHECK(has(t, "If it is not there, restart the reader."));
                    PB_CHECK(has(t, "may take noticeably longer"));
                    break;
                case RestartState::Needed:
                    PB_CHECK(has(t, "Restart the reader now to see it in General."));
                    PB_CHECK(has(t, "may take noticeably longer"));
                    break;
                case RestartState::NotNeeded:
                    PB_CHECK(has(t, "It will appear in General."));
                    PB_CHECK(!has(lower(t), "restart"));
                    PB_CHECK(!has(t, "noticeably longer"));
                    break;
            }
        }
}

PB_TEST(remove_names_the_data_folder_and_follows_the_restart_state) {
    DeviceFacts f;
    WordingArgs a = args();
    a.data_deleted = false;
    PB_CHECK_EQ(result_text(f, WordingOp::Remove, a), "PB Notes was removed. If it is still in the menu, restart the reader. Its data was kept in system/pbnotes.");
    a.data_deleted = true;
    PB_CHECK_EQ(result_text(f, WordingOp::Remove, a), "PB Notes was removed. If it is still in the menu, restart the reader. Its data was deleted.");
    f.restart_needed = Fact::No;
    PB_CHECK_EQ(result_text(f, WordingOp::Remove, a), "PB Notes was removed. Its data was deleted.");
    f.restart_needed = Fact::Yes;
    PB_CHECK(has(result_text(f, WordingOp::Remove, a), "Restart the reader now"));
    a.folder.clear();                                                // an app with no data folder says nothing about data
    a.data_deleted = false;
    PB_CHECK(!has(result_text(f, WordingOp::Remove, a), "data"));
}

PB_TEST(adopt_never_mentions_a_restart_for_any_combination) {
    for (Fact needed : kFacts)
        for (Fact shown : kFacts) {
            DeviceFacts f;
            f.restart_needed = needed;
            f.shown_without_restart = shown;
            PB_CHECK_EQ(result_text(f, WordingOp::Adopt, args()), "PB Notes is now managed by PB Store. Nothing else on the reader was changed.");
        }
}

PB_TEST(repair_says_what_was_done_and_follows_the_restart_state) {
    DeviceFacts f;
    WordingArgs a = args();
    PB_CHECK_EQ(result_text(f, WordingOp::Repair, a), "The interrupted update of PB Notes was finished. If the menu looks wrong, restart the reader.");
    a.repair_finished = false;
    PB_CHECK(has(result_text(f, WordingOp::Repair, a), "was undone."));
    f.restart_needed = Fact::No;
    PB_CHECK(!has(lower(result_text(f, WordingOp::Repair, a)), "restart"));
    f.restart_needed = Fact::Yes;
    PB_CHECK(has(result_text(f, WordingOp::Repair, a), "Restart the reader now"));
}

PB_TEST(the_self_update_text_is_about_opening_pb_store_again_never_about_restarting_the_reader) {
    for (Fact needed : kFacts)
        for (Fact shown : kFacts) {
            DeviceFacts f;
            f.restart_needed = needed;
            f.shown_without_restart = shown;
            const std::string t = result_text(f, WordingOp::SelfUpdate, args());
            PB_CHECK_EQ(t, "PB Store 0.2.0 is installed. It takes effect when you open PB Store again.");
            PB_CHECK(has(t, "when you open PB Store again"));
            PB_CHECK(!has(lower(t), "restart the reader"));
        }
}

PB_TEST(no_result_text_mentions_wifi) {
    DeviceFacts f;
    for (WordingOp op : {WordingOp::Install, WordingOp::Update, WordingOp::Remove, WordingOp::Adopt, WordingOp::Repair, WordingOp::SelfUpdate})
        PB_CHECK(!has(result_text(f, op, args()), "Wi-Fi"));
}

PB_TEST_MAIN()
