// T041: the note that lets the next start say "cancelled, nothing changed" (spec FR-025, data model PendingNote).
#include "mem_state_dirs.h"
#include "pbstore/install/state_dirs.h"
#include "pbstore/store/pending_note.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
}

PB_TEST(a_note_is_one_line_kind_app_from_to_with_a_dash_for_none) {
    pbtest::MemStateDirs dirs;
    PendingNote note(dirs);
    PB_CHECK(note.write({"install", "testapp", "", "1.2.0"}));
    PB_CHECK_EQ(dirs.text("pending-note.txt"), std::string("install testapp - 1.2.0\n"));
    PB_CHECK(note.write({"update", "testapp", "1.2.0", "1.3.0"}));
    PB_CHECK_EQ(dirs.text("pending-note.txt"), std::string("update testapp 1.2.0 1.3.0\n"));
    note.clear();
    PB_CHECK(!dirs.exists("pending-note.txt"));
    note.clear();                                                      // clearing nothing is fine
}

PB_TEST(at_start_a_note_without_a_journal_says_cancelled_nothing_changed_and_is_removed) {
    pbtest::MemStateDirs dirs;
    PendingNote(dirs).write({"install", "testapp", "", "1.2.0"});
    PendingNote note(dirs);
    auto f = note.at_start(/*journal_present=*/false);
    PB_CHECK(f.has_value());
    PB_CHECK_EQ(f->rule_id, std::string("W-STO-01"));
    PB_CHECK(f->severity == pbstore::Severity::Notice);
    PB_CHECK(has(f->message, "install"));
    PB_CHECK(has(f->message, "testapp"));
    PB_CHECK(has(f->message, "cancelled"));
    PB_CHECK(has(f->message, "nothing changed"));
    PB_CHECK(!dirs.exists("pending-note.txt"));
    PB_CHECK(!note.at_start(false).has_value());                       // said once
}

PB_TEST(with_a_journal_the_note_is_ignored_and_removed_because_recovery_speaks) {
    pbtest::MemStateDirs dirs;
    PendingNote(dirs).write({"update", "testapp", "1.2.0", "1.3.0"});
    PendingNote note(dirs);
    PB_CHECK(!note.at_start(/*journal_present=*/true).has_value());
    PB_CHECK(!dirs.exists("pending-note.txt"));
}

PB_TEST(no_note_no_message) {
    pbtest::MemStateDirs dirs;
    PendingNote note(dirs);
    PB_CHECK(!note.at_start(false).has_value());
    PB_CHECK(!note.at_start(true).has_value());
}

PB_TEST(a_malformed_or_oversized_note_is_removed_silently) {
    for (const std::string& bad : {std::string("nonsense"), std::string("install testapp"), std::string("a b c d e f\n"), std::string("\n"),
                                   std::string("install ") + std::string(300, 'x') + " - 1.0.0\n", std::string("install testapp - 1.0.0\nsecond line\n")}) {
        pbtest::MemStateDirs dirs;
        dirs.write_atomic("pending-note.txt", bad);
        PendingNote note(dirs);
        PB_CHECK(!note.at_start(false).has_value());
        PB_CHECK(!dirs.exists("pending-note.txt"));
    }
}

PB_TEST(a_note_is_at_most_256_bytes_and_a_longer_one_is_not_written) {
    pbtest::MemStateDirs dirs;
    PendingNote note(dirs);
    PB_CHECK(!note.write({"install", std::string(300, 'a'), "", "1.0.0"}));
    PB_CHECK(!dirs.exists("pending-note.txt"));
    PB_CHECK(!note.write({"install", "has space", "", "1.0.0"}));     // an id has no spaces: the line stays parsable
    PB_CHECK(!dirs.exists("pending-note.txt"));
}

PB_TEST(names_with_control_characters_are_neutralized_in_the_message) {
    pbtest::MemStateDirs dirs;
    dirs.write_atomic("pending-note.txt", std::string("install te\x1b[31mst - 1.0.0\n"));
    PendingNote note(dirs);
    auto f = note.at_start(false);
    PB_CHECK(f.has_value());
    PB_CHECK(!has(f->message, "\x1b"));
}

PB_TEST_MAIN()
