#include "pbstore/install/executor.h"

#include <algorithm>
#include <map>

#include "pbstore/install/backups.h"
#include "pbstore/install/fingerprint.h"
#include "pbstore/install/install_record.h"
#include "pbstore/install/journal.h"
#include "pbstore/install/recovery.h"
#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

namespace {

constexpr const char* kViewPath = "system/config/desktop/view.json";

// Thrown inside a run to leave it; caught in execute().
struct Abort {
    Finding finding;
    bool cancelled = false;
};

std::string mib(uint64_t bytes) {
    const uint64_t tenths = bytes * 10 / (1024 * 1024);
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MiB";
}

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

class Run {
public:
    Run(ReaderFs& fs, const Plan& plan, PackageContent& content, const ProgressFn& progress, CancelToken& cancel)
        : fs_(fs), plan_(plan), content_(content), progress_(progress), cancel_(cancel) {}

    ExecResult go(const Confirmations& given) {
        ExecResult res;
        for (Confirmation c : plan_.confirmations)
            if (!given.count(c)) res.missing_confirmations.push_back(c);
        if (!res.missing_confirmations.empty()) return res;
        try {
            if (cancel_.cancelled()) throw Abort{make_error("E-INS-33", "operation", "cancelled before anything was written"), true};
            check_plan();
            std::unique_ptr<Lock> lock = fs_.lock();
            if (!lock) throw Abort{make_error("E-INS-34", "reader", "another instance is working on this reader"), false};
            if (journal_exists(fs_))
                throw Abort{make_error("E-INS-23", "reader", "an earlier operation on this reader was interrupted and has to be repaired first"), false, };
            check_space();
            check_fingerprint();
            prepare();
            apply();
            commit();
            cleanup();
            res.outcome = ExecResult::Outcome::Done;
            res.cancel_too_late = cancel_.cancelled();
            return res;
        } catch (const Abort& a) {
            return abort_result(a);
        }
    }

private:
    // ---- checks before anything is written ----------------------------------------------------------------

    void check_plan() {
        auto bad_path = [&](const std::string& p) {
            throw Abort{make_error("E-INS-19", p, "the plan names a path outside the places the installer may write: " + p), false};
        };
        for (const PlanItem& it : plan_.items) {
            if (!starts_with(it.path, "applications/") || !valid_relative_path(it.path)) bad_path(it.path);
            if (it.op == ItemOp::Add || it.op == ItemOp::Replace) {
                std::string data;
                if (!content_.read(it.path, &data) || data.size() != it.size || sha256_hex(data) != it.sha256)
                    throw Abort{make_error("E-INS-37", it.path, "the content of " + it.path + " does not match the plan; nothing was written"), false};
            }
        }
        for (const std::string& d : plan_.directories_to_create) if (!starts_with(d, "applications/") || !valid_relative_path(d)) bad_path(d);
        for (const std::string& d : plan_.directories_to_remove) if (!starts_with(d, "applications/") || !valid_relative_path(d)) bad_path(d);
        if (plan_.app_id.empty()) bad_path("(no app id)");
    }

    void check_space() {
        const Space sp = fs_.space();
        if (sp.read_only) throw Abort{make_error("E-INS-05", "reader", "the storage of the reader is mounted read-only"), false};
        if (!sp.writable) throw Abort{make_error("E-INS-06", "reader", "this user cannot write to the reader's storage"), false};
        if (sp.free_bytes < plan_.space.needed)
            throw Abort{make_error("E-INS-07", "reader",
                                   "not enough free space on the reader: " + mib(plan_.space.needed) + " (" + std::to_string(plan_.space.needed) + " bytes) are needed and " +
                                       mib(sp.free_bytes) + " are free"),
                        false};
    }

    void check_fingerprint() {
        const FingerprintBasis now = read_basis(fs_, plan_target_paths(plan_));
        if (!basis_unchanged(plan_.fingerprint_basis, now))
            throw Abort{make_error("E-INS-25", "reader", "the reader changed since the plan was made: " + describe_basis_change(plan_.fingerprint_basis, now)), false};
    }

    // ---- helpers -------------------------------------------------------------------------------------------

    [[noreturn]] void storage_error(const std::string& subject, const std::string& what) {
        throw Abort{make_error("E-INS-37", subject, what), false};
    }
    void require(bool ok, const std::string& subject, const std::string& what) {
        if (!ok) storage_error(subject, what);
    }
    void poll_cancel() {
        if (!committed_ && cancel_.cancelled()) throw Abort{make_error("E-INS-33", "operation", "cancelled by the user"), true};
    }
    void report(const std::string& phase, uint64_t done, uint64_t total, const std::string& unit, const std::string& text) {
        if (progress_) {
            Progress p;
            p.phase = phase;
            p.done = done;
            p.total = total;
            p.unit = unit;
            p.text = text;
            progress_(p);
        }
        poll_cancel();
    }
    bool view_changes() const { return !plan_.view_new_text.empty(); }
    bool is_remove() const { return plan_.kind == PlanKind::Remove; }

    // ---- prepare: writes only inside the installer area --------------------------------------------------

    void prepare() {
        std::vector<const PlanItem*> staged;
        for (const PlanItem& it : plan_.items)
            if (it.op == ItemOp::Add || it.op == ItemOp::Replace) staged.push_back(&it);
        const uint64_t total = staged.size() + (view_changes() ? 1 : 0) + (plan_.record_change == RecordChange::Write || plan_.record_change == RecordChange::Replace ? 1 : 0);
        uint64_t done = 0;
        report("preparing", 0, total, "files", "Preparing the change");

        require(fs_.stat("system").kind == Kind::Dir, "system", "the folder system is missing on the reader");
        for (const char* d : {"system/pbstore", "system/pbstore/installed", "system/pbstore/journal"})
            require(ensure_directory(fs_, d), d, std::string("the folder ") + d + " could not be created");

        if (view_changes()) {   // a copy of the desktop configuration before it is touched
            std::string live;
            require(fs_.read_file(kViewPath, 64u << 20, &live), kViewPath, "the desktop configuration cannot be read");
            require(sha256_hex(live) == plan_.view_old_sha256, kViewPath, "the desktop configuration changed while the plan was made");
            require(write_backup(fs_, plan_.backup.name, live), kViewPath, "the backup of the desktop configuration could not be made; the configuration is not changed");
        }

        require(fs_.make_dir(kJournalDir), kJournalDir, "the journal could not be created");
        journal_made_ = true;
        intent_ = intent_from_plan(plan_);
        const std::string intent_file = intent_text(intent_);
        intent_sha_ = sha256_hex(intent_file);
        require(write_journal_file(fs_, kJournalDir, kIntentName, intent_file), kIntentName, "the journal intent could not be written");

        require(fs_.make_dir(journal_path("new")) && fs_.make_dir(journal_path("old")), kJournalDir, "the staging folders could not be created");
        for (const PlanItem* it : staged) {
            report("preparing", done, total, "files", "Copying " + it->path);
            std::string data;
            require(content_.read(it->path, &data), it->path, "the package content of " + it->path + " is not available");
            require(ensure_directory(fs_, parent_path(staged_path(it->path))), it->path, "a staging folder could not be created");
            require(fs_.create_exclusive(staged_path(it->path), data, false), it->path, "the file " + it->path + " could not be written to the reader");
            ++done;
        }
        for (const PlanItem& it : plan_.items) {
            if (it.op == ItemOp::Replace || it.op == ItemOp::Remove)
                require(ensure_directory(fs_, parent_path(vacated_path(it.path))), it.path, "a folder for the old files could not be created");
        }
        if (view_changes()) {
            report("preparing", done, total, "files", "Preparing the desktop configuration");
            require(fs_.create_exclusive(staged_path("view.json"), plan_.view_new_text, false), kViewPath, "the new desktop configuration could not be written");
            ++done;
        }
        if (plan_.record_change == RecordChange::Write || plan_.record_change == RecordChange::Replace) {
            report("preparing", done, total, "files", "Preparing the install record");
            require(fs_.create_exclusive(staged_path("record.json"), plan_.record_text, false), "install record", "the install record could not be written");
            ++done;
        }
        // read everything back before anything is applied
        report("preparing", total, total, "files", "Checking what was written");
        for (const PlanItem* it : staged) {
            std::string back;
            require(fs_.read_file(staged_path(it->path), it->size + 1, &back) && back.size() == it->size && sha256_hex(back) == it->sha256, it->path,
                    "the staged copy of " + it->path + " could not be read back correctly");
        }
        if (view_changes()) {
            std::string back;
            require(fs_.read_file(staged_path("view.json"), plan_.view_new_text.size() + 1, &back) && sha256_hex(back) == plan_.view_new_sha256, kViewPath,
                    "the staged desktop configuration could not be read back correctly");
        }
        if (plan_.record_change == RecordChange::Write || plan_.record_change == RecordChange::Replace) {
            std::string back;
            require(fs_.read_file(staged_path("record.json"), plan_.record_text.size() + 1, &back) && sha256_hex(back) == plan_.record_new_sha256, "install record",
                    "the staged install record could not be read back correctly");
        }
        require(fs_.flush_volume(), "reader", "the reader could not be flushed");
        require(write_journal_file(fs_, kJournalDir, kPreparedName, marker_text(kPreparedName, intent_sha_)), kPreparedName, "the prepared marker could not be written");
        prepared_ = true;
    }

    // ---- apply: a burst of renames, no copying of data -----------------------------------------------------

    void apply() {
        struct Step {
            std::string text;
            std::function<void()> run;
        };
        std::vector<Step> steps;
        auto add_dirs = [&] {
            for (const std::string& d : plan_.directories_to_create)
                steps.push_back({"Creating " + d, [this, d] { require(fs_.make_dir(d), d, "the folder " + d + " could not be created"); }});
        };
        auto add_items = [&] {
            for (const PlanItem& it : plan_.items) {
                const std::string p = it.path;
                if (it.op == ItemOp::Keep) continue;
                steps.push_back({"Placing " + p, [this, it, p] {
                                     if (it.op == ItemOp::Replace || it.op == ItemOp::Remove)
                                         require(fs_.rename_free(p, vacated_path(p)), p, "the old file " + p + " could not be moved aside");
                                     if (it.op == ItemOp::Add || it.op == ItemOp::Replace)
                                         require(fs_.rename_free(staged_path(p), p), p, "the file " + p + " could not be put in place");
                                 }});
            }
        };
        auto add_view = [&] {
            if (!view_changes()) return;
            steps.push_back({"Updating the desktop configuration", [this] {
                                 require(fs_.rename_free(kViewPath, vacated_path("view.json")), kViewPath, "the old desktop configuration could not be moved aside");
                                 require(fs_.rename_free(staged_path("view.json"), kViewPath), kViewPath, "the new desktop configuration could not be put in place");
                             }});
        };
        auto add_record = [&] {
            const std::string rec = record_path(plan_.app_id);
            if (plan_.record_change == RecordChange::None) return;
            steps.push_back({"Writing the install record", [this, rec] {
                                 if (plan_.record_change == RecordChange::Replace || plan_.record_change == RecordChange::Delete)
                                     require(fs_.rename_free(rec, vacated_path("record.json")), rec, "the old install record could not be moved aside");
                                 if (plan_.record_change == RecordChange::Write || plan_.record_change == RecordChange::Replace)
                                     require(fs_.rename_free(staged_path("record.json"), rec), rec, "the install record could not be put in place");
                             }});
        };
        auto add_removed_dirs = [&] {
            for (const std::string& d : plan_.directories_to_remove)
                steps.push_back({"Removing " + d, [this, d] {
                                     if (fs_.stat(d).kind == Kind::Dir && fs_.list(d).empty()) require(fs_.remove_dir(d), d, "the folder " + d + " could not be removed");
                                 }});
        };
        if (is_remove()) {
            add_view();
            add_items();
            add_record();
            add_removed_dirs();
        } else {
            add_dirs();
            add_items();
            add_view();
            add_record();
            add_removed_dirs();
        }
        const uint64_t total = steps.size();
        report("applying", 0, total, "steps", "Putting the change in place");
        uint64_t done = 0;
        for (Step& s : steps) {
            poll_cancel();
            applying_ = true;
            s.run();
            ++done;
            report("applying", done, total, "steps", s.text);
        }
        require(fs_.flush_volume(), "reader", "the reader could not be flushed");
    }

    // ---- commit and clean-up ---------------------------------------------------------------------------------

    void commit() {
        poll_cancel();
        const std::vector<std::string> problems = verify_live_state(fs_, intent_);
        if (!problems.empty()) storage_error(problems[0], "the reader does not hold what was written: " + problems[0]);
        poll_cancel();
        require(write_journal_file(fs_, kJournalDir, kCommitName, marker_text(kCommitName, intent_sha_)), kCommitName, "the commit marker could not be written");
        committed_ = true;
    }

    void cleanup() {
        report("finishing", 0, 0, "", "Cleaning up");
        delete_old_backups(fs_, intent_.delete_after_commit);
        // the clean-up is idempotent: one failed call is tried again before the change is reported as unfinished
        bool cleaned = delete_journal(fs_) && fs_.flush_volume();
        if (!cleaned) cleaned = delete_journal(fs_) && fs_.flush_volume();
        if (!cleaned) {
            cleanup_failed_ = true;
            throw Abort{make_error("E-INS-37", kJournalDir, "the change is complete but the clean-up could not be finished; run the repair to finish it"), false};
        }
        report("finishing", 0, 0, "", "Done");
    }

    // ---- leaving early ------------------------------------------------------------------------------------------

    ExecResult abort_result(const Abort& a) {
        ExecResult res;
        res.findings.push_back(a.finding);
        res.cancelled = a.cancelled;
        if (committed_) {   // the new version is complete; only the clean-up is left
            res.outcome = ExecResult::Outcome::RepairNeeded;
            res.effect = ReaderEffect::RepairNeeded;
            return res;
        }
        if (!journal_made_) {   // nothing was written to the live area, no journal
            res.outcome = ExecResult::Outcome::NotStarted;
            res.effect = a.finding.rule_id == "E-INS-23" ? ReaderEffect::RepairNeeded : ReaderEffect::Unchanged;
            return res;
        }
        // undo whatever was done (the same code as the repair of a later run)
        RepairResult u = prepared_ ? undo_journal(fs_, intent_) : discard_unprepared();
        for (const Finding& f : u.findings) res.findings.push_back(f);
        if (u.done) {
            res.outcome = ExecResult::Outcome::Undone;
            res.effect = applying_ ? ReaderEffect::Restored : ReaderEffect::Unchanged;
        } else {
            res.outcome = ExecResult::Outcome::RepairNeeded;
            res.effect = ReaderEffect::RepairNeeded;
            res.findings.push_back(make_error("E-INS-23", "reader", "the previous state could not be restored completely; repair is needed"));
        }
        return res;
    }

    RepairResult discard_unprepared() {
        RepairResult r;
        r.done = delete_journal(fs_) && fs_.flush_volume();
        r.outcome = "undone";
        if (!r.done) r.findings.push_back(make_error("E-INS-37", kJournalDir, "the leftovers of the attempt could not be removed"));
        return r;
    }

    ReaderFs& fs_;
    const Plan& plan_;
    PackageContent& content_;
    const ProgressFn& progress_;
    CancelToken& cancel_;
    Intent intent_;
    std::string intent_sha_;
    bool journal_made_ = false, prepared_ = false, applying_ = false, committed_ = false, cleanup_failed_ = false;
};

}  // namespace

ExecResult execute(ReaderFs& fs, const Plan& plan, PackageContent& content, const Confirmations& given, const ProgressFn& progress,
                   CancelToken& cancel) {
    Run run(fs, plan, content, progress, cancel);
    return run.go(given);
}

}  // namespace install
}  // namespace pbstore
