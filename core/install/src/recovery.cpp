#include "pbstore/install/recovery.h"

#include "pbstore/install/backups.h"
#include "pbstore/install/install_record.h"
#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

namespace {

constexpr const char* kViewPath = "system/config/desktop/view.json";
constexpr uint64_t kMaxHashed = 512ull * 1024 * 1024;

struct State {
    bool conflict = false;                // a path matched neither old nor new and was left alone
    bool io_error = false;                // a change could not be made
    std::vector<Finding> findings;
};

void conflict(State* st, const std::string& path, const std::string& why) {
    st->conflict = true;
    st->findings.push_back(make_error("E-INS-24", path, why));
}

// Hash of the file at `path`; false if it cannot be read.
bool file_hash(ReaderFs& fs, const std::string& path, std::string* hex) {
    std::string data;
    if (!fs.read_file(path, kMaxHashed, &data)) return false;
    *hex = sha256_hex(data);
    return true;
}

// Puts the old file back at `live` (hash first, idempotent). `new_hash` is what this operation put there,
// `old_hash` what was there before ("" if nothing). A file whose hash is neither is never removed.
void restore_path(ReaderFs& fs, State* st, const std::string& live, const std::string& vacated, const std::string& new_hash,
                  const std::string& old_hash) {
    const Stat old_stat = fs.stat(vacated);
    const bool old_present = old_stat.kind == Kind::File;
    const Stat live_stat = fs.stat(live);
    std::string live_hash;
    if (live_stat.kind == Kind::File && !file_hash(fs, live, &live_hash)) {
        conflict(st, live, live + " cannot be read; it is left as it is");
        return;
    }
    const bool live_is_new = live_stat.kind == Kind::File && !new_hash.empty() && live_hash == new_hash;
    const bool live_is_old = live_stat.kind == Kind::File && !old_hash.empty() && live_hash == old_hash;
    if (old_present) {
        if (live_stat.exists()) {
            if (!live_is_new) {
                conflict(st, live, live + " is neither the old nor the new file; it is left as it is");
                return;
            }
            if (!fs.remove_file(live)) {
                st->io_error = true;
                st->findings.push_back(make_error("E-INS-37", live, "the file " + live + " could not be removed"));
                return;
            }
        }
        if (!fs.rename_free(vacated, live)) {
            st->io_error = true;
            st->findings.push_back(make_error("E-INS-37", live, "the old file could not be put back at " + live));
        }
        return;
    }
    // no old file in the journal
    if (live_stat.exists()) {
        if (live_is_old) return;   // the old file is already back
        if (live_is_new) {
            if (!old_hash.empty()) {
                conflict(st, live, "the old version of " + live + " is missing from the journal; the new file is left as it is");
                return;
            }
            if (!fs.remove_file(live)) {
                st->io_error = true;
                st->findings.push_back(make_error("E-INS-37", live, "the file " + live + " could not be removed"));
            }
            return;
        }
        conflict(st, live, live + " is neither the old nor the new file; it is left as it is");
        return;
    }
    // neither a live file nor an old one
    if (!old_hash.empty()) conflict(st, live, "the old version of " + live + " is missing");
}

bool is_remove_kind(const Intent& in) { return in.kind == "remove"; }

}  // namespace

std::string RepairPlan::text() const {
    std::string t;
    if (action == Action::Discard) {
        t = "An earlier operation was interrupted before it changed anything.\n";
        t += "Repair: discard the leftovers of that operation.\n";
    } else {
        t = "An earlier " + kind + " of " + app_id;
        if (!from_version.empty() || !to_version.empty()) t += " (" + (from_version.empty() ? std::string("new") : from_version) + " -> " + (to_version.empty() ? std::string("removed") : to_version) + ")";
        t += " was interrupted.\n";
        t += std::string("Repair: ") + (action == Action::Undo ? "undo it and put the previous version back" : "finish it and clean up") + ".\n";
    }
    t += reason + "\n";
    return t;
}

RepairCheck plan_repair(ReaderFs& fs) {
    RepairCheck check;
    if (!journal_exists(fs)) return check;
    RepairPlan plan;
    std::string intent_file;
    const bool have_file = fs.read_file(journal_path(kIntentName), 4u << 20, &intent_file);
    IntentParse parsed;
    if (have_file) parsed = parse_intent(intent_file);
    if (parsed.newer_format) {
        check.findings.push_back(make_error("E-INS-35", kJournalDir, "the journal of an interrupted operation is from a newer format; the installer needs updating and has not touched it"));
        return check;
    }
    if (!parsed.intent) {
        plan.action = RepairPlan::Action::Discard;
        plan.reason = "The operation never got far enough to change anything on the reader, so only its leftovers are removed.";
        check.plan = plan;
        return check;
    }
    plan.intent = *parsed.intent;
    plan.intent_sha256 = parsed.sha256;
    plan.kind = plan.intent.kind;
    plan.app_id = plan.intent.app_id;
    plan.from_version = plan.intent.from_version;
    plan.to_version = plan.intent.to_version;
    std::string marker;
    if (fs.read_file(journal_path(kCommitName), 4096, &marker) && marker_valid(marker, kCommitName, parsed.sha256)) {
        plan.action = RepairPlan::Action::Finish;
        plan.reason = "The operation was complete except for the final clean-up; the new version is checked and the clean-up is finished.";
    } else if (fs.read_file(journal_path(kPreparedName), 4096, &marker) && marker_valid(marker, kPreparedName, parsed.sha256)) {
        plan.action = RepairPlan::Action::Undo;
        plan.reason = "The operation was interrupted before it was complete; the previous version is put back so that nothing is lost.";
    } else {
        plan.action = RepairPlan::Action::Discard;
        plan.reason = "The operation never got far enough to change anything on the reader, so only its leftovers are removed.";
    }
    check.plan = plan;
    return check;
}

RepairResult undo_journal(ReaderFs& fs, const Intent& in) {
    RepairResult r;
    State st;
    const std::string rec_live = record_path(in.app_id);

    auto restore_dirs_removed = [&] {
        // directories a removal had taken away come back, shallowest first
        for (size_t i = in.directories_to_remove.size(); i-- > 0;) {
            const std::string& d = in.directories_to_remove[i];
            if (!fs.stat(d).exists() && !fs.make_dir(d)) {
                st.io_error = true;
                st.findings.push_back(make_error("E-INS-37", d, "the directory " + d + " could not be created again"));
            }
        }
    };
    auto restore_record = [&] { restore_path(fs, &st, rec_live, vacated_path("record.json"), in.record_new_sha256, in.record_old_sha256); };
    auto restore_view = [&] {
        if (!in.view_new_sha256.empty() || !in.view_old_sha256.empty())
            restore_path(fs, &st, kViewPath, vacated_path("view.json"), in.view_new_sha256, in.view_old_sha256);
    };
    auto restore_items = [&] {
        for (size_t i = in.items.size(); i-- > 0;) {
            const IntentItem& it = in.items[i];
            restore_path(fs, &st, it.path, vacated_path(it.path), it.new_sha256, it.old_sha256);
        }
    };

    restore_dirs_removed();   // folders an update or a removal had taken away come back first: the old files go into them
    if (is_remove_kind(in)) {
        restore_record();
        restore_items();
        restore_view();
    } else {
        restore_record();
        restore_view();
        restore_items();
        // directories the operation created go, deepest first, if they are empty
        for (size_t i = in.directories_to_create.size(); i-- > 0;) {
            const std::string& d = in.directories_to_create[i];
            if (fs.stat(d).kind == Kind::Dir && fs.list(d).empty()) fs.remove_dir(d);
        }
    }
    if (st.conflict || st.io_error) {
        r.done = false;
        r.effect = ReaderEffect::RepairNeeded;
        r.findings = st.findings;
        r.outcome = "not finished";
        return r;
    }
    if (!fs.flush_volume() || !delete_journal(fs) || !fs.flush_volume()) {
        r.done = false;
        r.effect = ReaderEffect::RepairNeeded;
        r.findings.push_back(make_error("E-INS-37", kJournalDir, "the journal could not be removed after the previous version was put back"));
        r.outcome = "not finished";
        return r;
    }
    r.done = true;
    r.outcome = "undone";
    r.effect = ReaderEffect::Restored;
    return r;
}

RepairResult finish_journal(ReaderFs& fs, const Intent& in, const std::string& intent_sha256) {
    (void)intent_sha256;
    RepairResult r;
    const std::vector<std::string> problems = verify_live_state(fs, in);
    if (!problems.empty()) {
        if (fs.stat(journal_path("old")).exists()) {
            // the new version does not verify and the old files are still in the journal: go back
            RepairResult u = undo_journal(fs, in);
            for (const std::string& p : problems) u.findings.push_back(make_error("E-INS-24", p, "the new version did not verify: " + p));
            u.outcome = u.done ? "undone because the new version did not verify" : "not finished";
            return u;
        }
        for (const std::string& p : problems) r.findings.push_back(make_error("E-INS-24", p, "the new version did not verify and the old files are gone: " + p));
        r.effect = ReaderEffect::RepairNeeded;
        r.outcome = "not finished";
        return r;
    }
    if (!fs.flush_volume()) {
        r.findings.push_back(make_error("E-INS-37", "reader", "the reader could not be flushed"));
        r.effect = ReaderEffect::RepairNeeded;
        r.outcome = "not finished";
        return r;
    }
    delete_old_backups(fs, in.delete_after_commit);
    if (!delete_journal(fs) || !fs.flush_volume()) {
        r.findings.push_back(make_error("E-INS-37", kJournalDir, "the clean-up of the journal could not be finished"));
        r.effect = ReaderEffect::RepairNeeded;
        r.outcome = "not finished";
        return r;
    }
    r.done = true;
    r.outcome = "finished";
    return r;
}

RepairResult repair(ReaderFs& fs, const RepairPlan& given_plan) {
    (void)given_plan;
    RepairResult r;
    std::unique_ptr<Lock> lock = fs.lock();
    if (!lock) {
        r.findings.push_back(make_error("E-INS-34", "reader", "another instance is working on this reader"));
        return r;
    }
    // the state may have changed since the plan was shown: act on what is there now
    RepairCheck now = plan_repair(fs);
    if (!now.plan) {
        r.done = now.findings.empty();
        r.outcome = "nothing to repair";
        r.findings = now.findings;
        return r;
    }
    const RepairPlan& plan = *now.plan;
    switch (plan.action) {
        case RepairPlan::Action::Discard:
            if (!delete_journal(fs) || !fs.flush_volume()) {
                r.findings.push_back(make_error("E-INS-37", kJournalDir, "the leftovers of the interrupted operation could not be removed"));
                r.effect = ReaderEffect::RepairNeeded;
                r.outcome = "not finished";
                return r;
            }
            r.done = true;
            r.outcome = "undone";
            return r;
        case RepairPlan::Action::Undo:
            return undo_journal(fs, plan.intent);
        case RepairPlan::Action::Finish:
            return finish_journal(fs, plan.intent, plan.intent_sha256);
    }
    return r;
}

}  // namespace install
}  // namespace pbstore
