#include "pbstore/install/delete_data.h"

#include <cctype>

#include "pbstore/install/journal.h"
#include "pbstore/reserved_dirs.h"

namespace pbstore {
namespace install {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string human(uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    const char* units[] = {"KiB", "MiB", "GiB"};
    double v = static_cast<double>(bytes);
    int u = -1;
    while (v >= 1024.0 && u < 2) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f %s", v, units[u]);
    return buf;
}

// Deletes the files of a folder, then the folder, bottom up. Counts the files that went.
bool remove_folder(ReaderFs& fs, const std::string& dir, const ProgressFn& progress, CancelToken& cancel, uint64_t total, uint64_t* done, bool* cancelled) {
    bool ok = true;
    for (const DirEntry& e : fs.list(dir)) {
        const std::string p = join_path(dir, e.name);
        if (e.kind == Kind::Dir) {
            ok = remove_folder(fs, p, progress, cancel, total, done, cancelled) && ok;
        } else {
            if (cancel.cancelled()) {
                *cancelled = true;
                return false;
            }
            ok = fs.remove_file(p) && ok;   // a link is removed itself, never followed
            ++*done;
            if (progress) {
                Progress pr;
                pr.phase = "applying";
                pr.done = *done;
                pr.total = total;
                pr.unit = "files";
                pr.text = "Deleting the data folder";
                progress(pr);
            }
            if (cancel.cancelled()) {
                *cancelled = true;
                return false;
            }
        }
        if (*cancelled) return false;
    }
    return fs.remove_dir(dir) && ok;
}

}  // namespace

DeleteDataPlanResult plan_delete_data(const std::string& app_id, const std::optional<std::string>& data_dir, const ReaderSnapshot& snap) {
    DeleteDataPlanResult r;
    if (!data_dir || data_dir->empty()) {
        r.notices.push_back(make_notice("W-INS-01", app_id, app_id + " has no data folder; there is nothing to delete"));
        return r;
    }
    const std::string& dir = *data_dir;
    const std::string name = dir.size() > 7 ? dir.substr(7) : "";
    if (dir.compare(0, 7, "system/") != 0 || name.empty() || name.find('/') != std::string::npos) {
        r.findings.push_back(make_error("E-INS-19", dir, "the path " + dir + " is not a data folder (system/<name>)"));
        return r;
    }
    if (is_reserved_data_dir(lower(name)) || lower(name) == "pbstore") {
        r.findings.push_back(make_error("E-INS-18", dir, "the folder " + dir + " is a reserved system folder and is never deleted"));
        return r;
    }
    for (const auto& kv : snap.installed)
        if (kv.second.record.data_dir && lower(*kv.second.record.data_dir) == lower(dir)) {
            r.findings.push_back(make_error("E-INS-16", dir, "the data folder " + dir + " is also used by " + kv.first + ", so it is not deleted"));
            return r;
        }
    auto fact = snap.facts.find(dir);
    const Kind kind = fact == snap.facts.end() ? Kind::None : fact->second.kind;
    if (kind == Kind::Link) {
        r.findings.push_back(make_error("E-INS-22", dir, "the data folder " + dir + " is a link-like object; the installer never follows links"));
        return r;
    }
    if (kind == Kind::None) {
        r.notices.push_back(make_notice("W-INS-01", dir, "the data folder " + dir + " does not exist; there is nothing to delete"));
        return r;
    }
    if (kind != Kind::Dir) {
        r.findings.push_back(make_error("E-INS-19", dir, dir + " is not a folder"));
        return r;
    }
    Plan p;
    p.kind = PlanKind::DeleteData;
    p.app_id = app_id;
    p.source_label = "the data folder of an app that was removed";
    DataDirPlan d;
    d.path = dir;
    d.exists = true;
    auto data = snap.data.find(dir);
    if (data != snap.data.end()) {
        d.files = data->second.files;
        d.bytes = data->second.bytes;
    }
    p.data = d;
    p.confirmations = {Confirmation::DeleteData1, Confirmation::DeleteData2};
    r.plan = p;
    return r;
}

std::string delete_data_token_1(const Plan& plan) { return plan.data ? plan.data->path : std::string(); }
std::string delete_data_token_2(const Plan& plan) { return plan.data ? std::to_string(plan.data->files) : std::string(); }

std::string delete_data_question_1(const Plan& plan) {
    return "Delete the data folder? This cannot be undone. To confirm, type the folder exactly as shown: " + delete_data_token_1(plan);
}

std::string delete_data_question_2(const Plan& plan) {
    return "This deletes " + delete_data_token_2(plan) + " files (" + human(plan.data ? plan.data->bytes : 0) + ") for good. To confirm, type that number of files: " + delete_data_token_2(plan);
}

DeleteResult delete_data(ReaderFs& fs, const Plan& plan, const std::string& token1, const std::string& token2, const ProgressFn& progress, CancelToken& cancel) {
    DeleteResult r;
    if (plan.kind != PlanKind::DeleteData || !plan.data) {
        r.findings.push_back(make_error("E-INS-19", plan.app_id, "this is not a plan for deleting a data folder"));
        return r;
    }
    r.wrong_token_1 = token1 != delete_data_token_1(plan);
    r.wrong_token_2 = token2 != delete_data_token_2(plan);
    if (r.wrong_token_1 || r.wrong_token_2) return r;
    if (cancel.cancelled()) {
        r.cancelled = true;
        r.findings.push_back(make_error("E-INS-33", plan.data->path, "cancelled by the user; the data folder was not changed"));
        return r;
    }
    std::unique_ptr<Lock> lock = fs.lock();
    if (!lock) {
        r.findings.push_back(make_error("E-INS-34", "reader", "another instance is working on this reader"));
        return r;
    }
    if (journal_exists(fs)) {
        r.findings.push_back(make_error("E-INS-23", "reader", "an earlier operation on this reader was interrupted and has to be repaired first"));
        return r;
    }
    const Stat st = fs.stat(plan.data->path);
    if (st.kind != Kind::Dir) {
        r.findings.push_back(make_error("E-INS-25", plan.data->path, "the data folder " + plan.data->path + " is no longer a folder; make a new plan"));
        return r;
    }
    const DataFolderFacts now = count_data_folder(fs, plan.data->path);
    if (now.files != plan.data->files || now.bytes != plan.data->bytes) {
        r.findings.push_back(make_error("E-INS-25", plan.data->path,
                                        "the data folder changed since the plan was made: it holds " + std::to_string(now.files) + " files now, the plan said " +
                                            std::to_string(plan.data->files)));
        return r;
    }
    uint64_t done = 0;
    bool cancelled = false;
    const bool ok = remove_folder(fs, plan.data->path, progress, cancel, plan.data->files, &done, &cancelled);
    r.files_deleted = done;
    if (cancelled) {
        r.cancelled = true;
        r.findings.push_back(make_error("E-INS-33", plan.data->path, "cancelled by the user; the data folder is partly deleted, run the step again to finish it"));
        return r;
    }
    if (!ok || !fs.flush_volume()) {
        r.findings.push_back(make_error("E-INS-37", plan.data->path, "the data folder could not be deleted completely"));
        return r;
    }
    r.done = true;
    return r;
}

}  // namespace install
}  // namespace pbstore
