#pragma once
// The journal of one operation on a reader (contracts/journal.md): an intent, two markers and the staged
// and vacated files, all under system/pbstore/journal/current/.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/plan.h"
#include "pbstore/install/reader_fs.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

constexpr const char* kJournalDir = "system/pbstore/journal/current";
constexpr const char* kIntentName = "10-intent.json";
constexpr const char* kPreparedName = "20-prepared";
constexpr const char* kCommitName = "30-commit";
constexpr int kJournalFormat = 1;

struct IntentItem {
    std::string path;
    std::string new_sha256;   // "" if the file is only vacated
    uint64_t new_size = 0;
    std::string old_sha256;   // "" if there is no old file
    bool vacate = false;      // an old file is moved aside
};

struct Intent {
    int journal_format = kJournalFormat;
    std::string kind;                         // install, update, remove, adopt, replace
    std::string app_id, from_version, to_version;
    std::vector<IntentItem> items;            // in the order they are applied
    std::vector<std::string> directories_to_create;
    std::vector<std::string> directories_to_remove;
    std::string launcher;
    std::string view_old_sha256, view_new_sha256;       // both "" if view.json does not change
    std::string record_old_sha256, record_new_sha256;   // "" if none
    std::string backup_name;
    std::vector<std::string> delete_after_commit;
};

Intent intent_from_plan(const Plan& plan);

std::string intent_text(const Intent& intent);   // JSON followed by the "sha256:" line
struct IntentParse {
    std::optional<Intent> intent;
    bool newer_format = false;                   // journal_format above 1: not touched (E-INS-35)
    std::string sha256;                          // of the whole file, which the markers refer to
};
// A file without a matching last line, or with other problems, gives no intent (it is "absent").
IntentParse parse_intent(const std::string& text);

// Markers: "<name> <intent sha256>\n" plus the "sha256:" line.
std::string marker_text(const std::string& name, const std::string& intent_sha256);
bool marker_valid(const std::string& text, const std::string& name, const std::string& intent_sha256);

// Adds the line "sha256:<hex of everything above>\n" / checks it; the body is returned without the line.
std::string with_checksum_line(const std::string& body);
bool strip_checksum_line(const std::string& text, std::string* body);

// Writes a journal file the safe way: temporary name, flush, rename to a free name, flush the directory.
// The temporary name is `<name>.tmp`.
bool write_journal_file(ReaderFs& fs, const Path& dir, const std::string& name, const std::string& content);

// Paths inside the journal.
std::string journal_path(const std::string& relative);   // system/pbstore/journal/current/<relative>
std::string staged_path(const std::string& path);        // .../new/<path>
std::string vacated_path(const std::string& path);       // .../old/<path>

bool journal_exists(ReaderFs& fs);
// What is wrong with the live area compared with what the intent says it must be after the operation:
// one sentence per path (empty when everything is as intended). Reads hashes; parses the desktop configuration.
std::vector<std::string> verify_live_state(ReaderFs& fs, const Intent& intent);
// Deletes the journal folder with everything in it (files, then directories). Never follows links. The order
// keeps every intermediate state classifiable by the recovery: staged and vacated files first, then the
// prepared marker, the intent, the commit marker, and last the folder.
bool delete_journal(ReaderFs& fs);
// Deletes a directory tree below the reader's root, bottom up. False if something could not be removed.
bool delete_tree(ReaderFs& fs, const Path& dir);

}  // namespace install
}  // namespace pbstore
