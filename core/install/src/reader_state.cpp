#include "pbstore/install/reader_state.h"

#include <algorithm>
#include <set>

#include "pbstore/install/backups.h"
#include "pbstore/install/journal.h"
#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

namespace {

constexpr const char* kViewPath = "system/config/desktop/view.json";
constexpr const char* kRecordDir = "system/pbstore/installed";
constexpr size_t kMaxCountedEntries = 2000000;

void add_with_ancestors(std::set<std::string>* out, const std::string& path) {
    out->insert(path);
    for (std::string p = parent_path(path); !p.empty(); p = parent_path(p)) out->insert(p);
}

DataFolderFacts count_folder(ReaderFs& fs, const std::string& dir, size_t* budget) {
    DataFolderFacts f;
    const Stat st = fs.stat(dir);
    f.exists = st.exists();
    if (st.kind != Kind::Dir) return f;
    for (const DirEntry& e : fs.list(dir)) {
        if (*budget == 0) break;
        --*budget;
        if (e.kind == Kind::Dir) {
            DataFolderFacts sub = count_folder(fs, join_path(dir, e.name), budget);
            f.files += sub.files;
            f.bytes += sub.bytes;
        } else {
            ++f.files;
            f.bytes += e.size;   // the size from the directory entry: the file is not opened
        }
    }
    return f;
}

}  // namespace

DataFolderFacts count_data_folder(ReaderFs& fs, const std::string& dir) {
    size_t budget = kMaxCountedEntries;
    return count_folder(fs, dir, &budget);
}

std::vector<std::string> scan_target_paths(const ScanRequest& req, const std::map<std::string, InstalledApp>& installed) {
    std::set<std::string> paths;
    if (req.manifest) {
        for (const FileEntry& f : req.manifest->files) add_with_ancestors(&paths, f.path);
        if (req.manifest->data_dir) paths.insert(*req.manifest->data_dir);
    }
    if (!req.data_dir.empty()) paths.insert(req.data_dir);
    if (!req.app_id.empty()) {
        paths.insert(record_path(req.app_id));
        auto it = installed.find(req.app_id);
        if (it != installed.end()) {
            for (const FileEntry& f : it->second.record.files) add_with_ancestors(&paths, f.path);
            for (const std::string& d : it->second.record.directories) add_with_ancestors(&paths, d);
            if (it->second.record.data_dir) paths.insert(*it->second.record.data_dir);
        }
    }
    if (req.all_recorded)
        for (const auto& kv : installed)
            for (const FileEntry& f : kv.second.record.files) paths.insert(f.path);
    paths.erase("");
    return std::vector<std::string>(paths.begin(), paths.end());
}

ReaderSnapshot scan(ReaderFs& fs, const Identity& identity, const ScanRequest& req) {
    ReaderSnapshot s;
    s.identity = identity;
    s.space = fs.space();

    // the desktop configuration
    if (!fs.read_file(kViewPath, 64u << 20, &s.view_text)) {
        s.view_text.clear();
        s.findings.push_back(make_error("E-INS-12", kViewPath, "the desktop configuration cannot be read"));
    } else {
        s.view_sha256 = sha256_hex(s.view_text);
        ParseResult p = parse_desktop_config(s.view_text);
        if (p.config) s.desktop = std::move(p.config);
        else for (const Finding& f : p.findings) s.findings.push_back(f);
    }

    // the install records
    for (const DirEntry& e : fs.list(kRecordDir)) {
        if (e.kind != Kind::File || e.name.size() <= 5 || e.name.compare(e.name.size() - 5, 5, ".json") != 0) continue;
        const std::string app = e.name.substr(0, e.name.size() - 5);
        std::string text;
        const std::string path = join_path(kRecordDir, e.name);
        if (!fs.read_file(path, kMaxRecordBytes + 1, &text)) {
            s.findings.push_back(make_error("E-INS-35", path, "the install record " + path + " cannot be read"));
            continue;
        }
        RecordParse p = parse_install_record(text, app);
        if (p.record) {
            InstalledApp a;
            a.record = std::move(*p.record);
            s.installed[app] = std::move(a);
        } else {
            for (const Finding& f : p.findings) s.findings.push_back(f);
        }
    }

    for (const DirEntry& e : fs.list(kBackupDir))
        if (e.kind == Kind::File) s.backups.push_back(e.name);
    s.journal_present = journal_exists(fs);

    // the paths a plan depends on
    const std::vector<std::string> targets = scan_target_paths(req, s.installed);
    std::set<std::string> parents;
    for (const std::string& t : targets) {
        s.facts[t] = read_fact(fs, t);
        parents.insert(parent_path(t));
    }
    for (const std::string& dir : parents) {
        if (dir.empty()) {
            s.listings[dir] = fs.list(dir);
        } else {
            auto it = s.facts.find(dir);
            if (it != s.facts.end() && it->second.kind == Kind::Dir) s.listings[dir] = fs.list(dir);
        }
    }

    // data folders: counted, never read
    std::set<std::string> data_dirs;
    if (req.manifest && req.manifest->data_dir) data_dirs.insert(*req.manifest->data_dir);
    if (!req.data_dir.empty()) data_dirs.insert(req.data_dir);
    for (const auto& kv : s.installed)
        if (kv.second.record.data_dir) data_dirs.insert(*kv.second.record.data_dir);
    for (const std::string& d : data_dirs) {
        size_t budget = kMaxCountedEntries;
        s.data[d] = count_folder(fs, d, &budget);
    }

    // which recorded files differ from their record
    if (req.all_recorded) {
        for (auto& kv : s.installed) {
            for (const FileEntry& f : kv.second.record.files) {
                const TargetFact& fact = s.facts[f.path];
                if (fact.kind != Kind::File || fact.sha256 != f.sha256) kv.second.changed_files.push_back(f.path);
            }
        }
    } else if (!req.app_id.empty()) {
        auto it = s.installed.find(req.app_id);
        if (it != s.installed.end())
            for (const FileEntry& f : it->second.record.files) {
                const TargetFact& fact = s.facts[f.path];
                if (fact.kind != Kind::File || fact.sha256 != f.sha256) it->second.changed_files.push_back(f.path);
            }
    }
    return s;
}

FingerprintBasis snapshot_basis(const ReaderSnapshot& snap, const std::vector<std::string>& plan_paths) {
    return make_basis(snap.view_sha256, snap.facts, plan_paths, snap.space.free_bytes);
}

}  // namespace install
}  // namespace pbstore
