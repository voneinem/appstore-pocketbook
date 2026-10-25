#include <algorithm>

#include "cli.h"
#include "pbstore/install/text_safety.h"
#include "progress_terminal.h"
#include "prompts.h"
#include "signals.h"

namespace pbstore {
namespace install {
namespace cli {

std::string human_size(uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    const char* units[] = {"KiB", "MiB", "GiB", "TiB"};
    double v = static_cast<double>(bytes);
    int u = -1;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f %s", v, units[u]);
    return buf;
}

int print_findings(Context& ctx, const std::vector<Finding>& findings, ReaderEffect effect) {
    if (findings.empty()) return 0;
    std::vector<std::string> nexts;
    bool repair_rule = false;
    for (const Finding& f : findings) {
        const InstallRule* r = find_install_rule(f.rule_id);
        repair_rule = repair_rule || (r && r->effect == ReaderEffect::RepairNeeded);
    }
    for (const Finding& f : findings) {
        ctx.err << neutralize(format_finding(f)) << "\n";
        // when the reader needs repair, the way to repair it is the one next step that matters
        const InstallRule* r = find_install_rule(f.rule_id);
        if (effect == ReaderEffect::RepairNeeded && repair_rule && !(r && r->effect == ReaderEffect::RepairNeeded)) continue;
        const Message m = describe(f, effect);
        if (!m.next.empty() && std::find(nexts.begin(), nexts.end(), m.next) == nexts.end()) nexts.push_back(m.next);
    }
    ctx.err << reader_effect_text(effect) << "\n";
    for (const std::string& n : nexts) ctx.err << "Next: " << n << "\n";
    return exit_status_for(findings);
}

void print_notices(Context& ctx, const std::vector<Finding>& notices) {
    for (const Finding& n : notices)
        ctx.err << (n.severity == Severity::Notice ? "notice" : "warning") << "[" << n.rule_id << "] " << neutralize(n.subject) << ": " << neutralize(n.message) << "\n";
}

bool open_reader_for(Context& ctx, const Options& opts, std::shared_ptr<ReaderHandle>* reader, int* status) {
    auto r = ctx.engine.open_reader(opts.reader);
    if (!r.ok()) {
        *status = print_findings(ctx, r.findings, ReaderEffect::Unchanged);
        return false;
    }
    *reader = *r.value;
    return true;
}

int handle_interrupted(Context& ctx, const Options& opts, ReaderHandle& reader, bool* handled) {
    *handled = false;
    RepairCheck rc = ctx.engine.repair_plan(reader);
    if (!rc.plan) {
        if (!rc.findings.empty()) {
            *handled = true;
            return print_findings(ctx, rc.findings, ReaderEffect::Unchanged);
        }
        return 0;
    }
    *handled = true;
    ctx.out << neutralize(rc.plan->text(), true);
    if (opts.dry_run && opts.command == "repair") {
        ctx.out << "This was a dry run: nothing was written to the reader.\n";
        return 0;
    }
    const bool confirmed = opts.confirmed.count(Confirmation::Repair) > 0 || (ctx.interactive && ask_yes_no("Carry out this repair now?"));
    if (!confirmed) {
        const Finding f = make_error("E-INS-23", rc.plan->app_id.empty() ? "reader" : rc.plan->app_id, "an earlier operation on this reader was interrupted and has to be repaired first");
        return print_findings(ctx, {f}, ReaderEffect::RepairNeeded);
    }
    CancelToken cancel;
    RepairResult rr = ctx.engine.repair(reader, terminal_progress(), cancel);
    finish_terminal_progress();
    if (rr.done) {
        ctx.out << "The interrupted operation was " << rr.outcome << ". The reader is in one complete state again.\n";
        return 0;
    }
    print_findings(ctx, rr.findings, ReaderEffect::RepairNeeded);
    return 4;
}

int cmd_readers(Context& ctx, const Options&) {
    LocateResult lr = ctx.engine.readers();
    if (lr.candidates.empty()) ctx.out << "No drive that could be the reader was found.\n";
    for (const Candidate& c : lr.candidates) {
        ctx.out << neutralize(c.label) << "  " << neutralize(c.mount_point) << "  " << (c.size_bytes ? human_size(c.size_bytes) : std::string("size unknown")) << "  ";
        if (c.qualifies) {
            ctx.out << "reader\n";
        } else {
            ctx.out << "not a reader (missing:";
            for (const std::string& m : c.missing) ctx.out << " " << m;
            ctx.out << ")\n";
        }
    }
    for (const std::string& n : lr.notes) ctx.out << n << "\n";
    if (lr.pocketbook_on_usb && lr.candidates.empty()) ctx.out << "A PocketBook seems to be connected, but no storage of it is available; set USB storage mode on the reader.\n";
    return 0;
}

namespace {

const char* state_words(const AppEntry& a) {
    switch (a.state) {
        case AppStateKind::NotInstalled: return "not installed";
        case AppStateKind::Installed: return a.update_available ? "installed, update available" : "installed";
        case AppStateKind::OutsideInstaller: return "installed outside the installer";
        case AppStateKind::UnknownApp: return "on the reader, not in the catalog";
    }
    return "";
}

void print_apps(Context& ctx, const std::vector<AppEntry>& apps) {
    for (const AppEntry& a : apps) {
        ctx.out << neutralize(a.id) << "\t" << neutralize(a.name) << "\t" << state_words(a) << "\t";
        ctx.out << (a.installed_version.empty() ? std::string("-") : "installed " + a.installed_version) << "\t";
        ctx.out << (a.offered_version.empty() ? std::string("-") : "offered " + a.offered_version + (a.offered ? "" : " (withdrawn)")) << "\t";
        std::string notes;
        if (!a.source.empty()) notes += "source " + a.source;
        if (a.from_local) notes += (notes.empty() ? "" : "; ") + std::string("not from the catalog, not signed by the maintainer");
        if (a.changed) notes += (notes.empty() ? "" : "; ") + std::string("files changed since the install");
        if (a.installed_version_withdrawn) notes += (notes.empty() ? "" : "; ") + std::string("withdrawn in the catalog");
        if (a.menu_entry_missing) notes += (notes.empty() ? "" : "; ") + std::string("menu entry missing");
        if (!a.description.empty()) notes += (notes.empty() ? "" : "; ") + neutralize(a.description);
        ctx.out << notes << "\n";
    }
}

}  // namespace

int cmd_installed(Context& ctx, const Options& opts) {
    std::shared_ptr<ReaderHandle> reader;
    int status = 0;
    if (!open_reader_for(ctx, opts, &reader, &status)) return status;
    if (opts.confirmed.count(Confirmation::Repair)) {
        bool handled = false;
        const int s = handle_interrupted(ctx, opts, *reader, &handled);
        if (handled) return s;
    }
    auto r = ctx.engine.installed(*reader);
    print_notices(ctx, r.notices);
    if (!r.ok()) return print_findings(ctx, r.findings, ReaderEffect::Unchanged);
    if (r.value->apps.empty()) ctx.out << "No apps were found on the reader.\n";
    print_apps(ctx, r.value->apps);
    if (r.value->interrupted) {
        ctx.out << neutralize(r.value->interrupted->text(), true);
        const Finding f = make_error("E-INS-23", r.value->interrupted->app_id.empty() ? "reader" : r.value->interrupted->app_id, "an earlier operation on this reader was interrupted and has to be repaired");
        print_findings(ctx, {f}, ReaderEffect::RepairNeeded);
        return 4;
    }
    return 0;
}

int cmd_list(Context& ctx, const Options& opts) {
    std::shared_ptr<ReaderHandle> reader;
    int status = 0;
    if (!open_reader_for(ctx, opts, &reader, &status)) return status;
    bool handled = false;
    const int s = handle_interrupted(ctx, opts, *reader, &handled);
    if (handled) return s;
    CancelToken cancel;
    SigintCancel sigint(&cancel);
    auto r = ctx.engine.list(*reader, terminal_progress(), cancel);
    finish_terminal_progress();
    print_notices(ctx, r.notices);
    if (!r.ok()) return print_findings(ctx, r.findings, ReaderEffect::Unchanged);
    ctx.out << "Catalog generated " << r.value->catalog_generated_at.substr(0, 10) << "\n";
    if (r.value->apps.empty()) ctx.out << "The catalog lists no apps and the reader has none.\n";
    bool in_catalog = false;
    for (const AppEntry& a : r.value->apps) in_catalog = in_catalog || !a.offered_version.empty();
    if (!in_catalog) ctx.out << "The catalog lists no apps.\n";
    print_apps(ctx, r.value->apps);
    return 0;
}

int cmd_repair(Context& ctx, const Options& opts) {
    std::shared_ptr<ReaderHandle> reader;
    int status = 0;
    if (!open_reader_for(ctx, opts, &reader, &status)) return status;
    bool handled = false;
    const int s = handle_interrupted(ctx, opts, *reader, &handled);
    if (handled) return s;
    ctx.out << "Nothing to repair: no operation was left unfinished on this reader.\n";
    return 0;
}

}  // namespace cli
}  // namespace install
}  // namespace pbstore
