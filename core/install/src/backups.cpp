#include "pbstore/install/backups.h"

#include <algorithm>
#include <cctype>

namespace pbstore {
namespace install {

namespace {
bool digits(const std::string& s, size_t from, size_t n) {
    if (from + n > s.size()) return false;
    for (size_t i = from; i < from + n; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}
}  // namespace

std::string backup_name(const LocalTime& now) { return "view-" + now.stamp() + ".json"; }

bool is_backup_name(const std::string& name) {
    // view-YYYYMMDDTHHMMSS+hhmm.json
    if (name.size() != 5 + 8 + 1 + 6 + 5 + 5) return false;
    if (name.compare(0, 5, "view-") != 0) return false;
    if (!digits(name, 5, 8) || name[13] != 'T' || !digits(name, 14, 6)) return false;
    if (name[20] != '+' && name[20] != '-') return false;
    if (!digits(name, 21, 4)) return false;
    return name.compare(25, 5, ".json") == 0;
}

std::string backup_day(const std::string& name) { return is_backup_name(name) ? name.substr(5, 8) : std::string(); }

BackupPlan plan_backup_from(const std::vector<std::string>& existing_names, const LocalTime& at) {
    BackupPlan p;
    // two operations within the same second must not share a name: take the next free second of the day
    LocalTime now = at;
    for (int tries = 0; tries < 120; ++tries) {
        const std::string candidate = backup_name(now);
        if (std::find(existing_names.begin(), existing_names.end(), candidate) == existing_names.end()) break;
        if (++now.second == 60) {
            now.second = 0;
            if (++now.minute == 60) {
                now.minute = 0;
                if (now.hour < 23) ++now.hour;
            }
        }
    }
    p.name = backup_name(now);
    const std::string day = now.day_key();
    for (const std::string& n : existing_names)
        if (is_backup_name(n) && backup_day(n) == day && n != p.name) p.delete_after_commit.push_back(n);
    std::sort(p.delete_after_commit.begin(), p.delete_after_commit.end());
    return p;
}

BackupPlan plan_backup(ReaderFs& fs, const LocalTime& now) {
    std::vector<std::string> names;
    for (const DirEntry& e : fs.list(kBackupDir))
        if (e.kind == Kind::File) names.push_back(e.name);
    return plan_backup_from(names, now);
}

bool write_backup(ReaderFs& fs, const std::string& name, const std::string& view_text) {
    if (!is_backup_name(name)) return false;
    if (!ensure_directory(fs, kBackupDir)) return false;
    const std::string path = join_path(kBackupDir, name);
    if (!fs.create_exclusive(path, view_text, true)) return false;
    std::string back;
    return fs.read_file(path, view_text.size() + 1, &back) && back == view_text;
}

int delete_old_backups(ReaderFs& fs, const std::vector<std::string>& names) {
    int deleted = 0;
    for (const std::string& n : names) {
        if (!is_backup_name(n)) continue;
        const std::string path = join_path(kBackupDir, n);
        if (fs.stat(path).kind == Kind::File && fs.remove_file(path)) ++deleted;
    }
    return deleted;
}

}  // namespace install
}  // namespace pbstore
