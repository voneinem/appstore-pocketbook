#pragma once
// The note that lets the next start say "cancelled, nothing changed" (spec FR-025; data model PendingNote). One line
// `<kind> <app-id> <from|-> <to|->` of at most 256 bytes, written before the first write of an operation and removed when its
// result is shown. At the next start a note without a journal means the operation was cancelled before its commit point and
// undone; with a journal, recovery speaks and the note is only removed.

#include <optional>
#include <string>

#include "pbstore/install/state_dirs.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace store {

struct NoteContent {
    std::string kind;      // install, update, remove, adopt, repair
    std::string app_id;
    std::string from;      // "" for none
    std::string to;
};

class PendingNote {
public:
    explicit PendingNote(install::StateDirs& dirs) : dirs_(dirs) {}
    // False when the line would be longer than 256 bytes or a part holds a space or a line break.
    bool write(const NoteContent& note);
    void clear();
    // W-STO-01 for a valid note without a journal. The note is removed in every case; a malformed or oversized one silently.
    std::optional<Finding> at_start(bool journal_present);

private:
    install::StateDirs& dirs_;
};

}  // namespace store
}  // namespace pbstore
