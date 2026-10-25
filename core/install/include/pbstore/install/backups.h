#pragma once
// Backups of the desktop configuration: a full copy before every change, named by local date, time and
// offset. Of the backups the installer made only the newest per local calendar day is kept.

#include <string>
#include <vector>

#include "pbstore/install/clock.h"
#include "pbstore/install/plan.h"
#include "pbstore/install/reader_fs.h"

namespace pbstore {
namespace install {

constexpr const char* kBackupDir = "system/pbstore/backups";

// view-<YYYYMMDD>T<HHMMSS><+-hhmm>.json
std::string backup_name(const LocalTime& now);
// True for names of exactly that form (other files in the folder are never touched).
bool is_backup_name(const std::string& name);
// The local calendar day (YYYYMMDD) a backup name belongs to; "" if it is not a backup name.
std::string backup_day(const std::string& name);

// The same from a list of the names in the backups folder (pure).
BackupPlan plan_backup_from(const std::vector<std::string>& existing_names, const LocalTime& now);
// The new backup's name and the older backups of the same local day that will be deleted after the commit.
BackupPlan plan_backup(ReaderFs& fs, const LocalTime& now);

// Writes the copy of the desktop configuration, reads it back and compares. False if it could not be made.
bool write_backup(ReaderFs& fs, const std::string& name, const std::string& view_text);
// Deletes the named older backups, only if they match the name pattern. Returns how many were deleted.
int delete_old_backups(ReaderFs& fs, const std::vector<std::string>& names);

}  // namespace install
}  // namespace pbstore
