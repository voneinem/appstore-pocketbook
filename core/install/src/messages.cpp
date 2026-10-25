#include "pbstore/install/messages.h"

#include "pbstore/install/text_safety.h"

namespace pbstore {
namespace install {

std::string reader_effect_text(ReaderEffect effect) {
    switch (effect) {
        case ReaderEffect::Unchanged: return "The reader was not changed.";
        case ReaderEffect::Restored: return "The reader was restored to the previous state.";
        case ReaderEffect::RepairNeeded: return "The reader needs repair (repair needed): an operation was left unfinished.";
    }
    return "";
}

namespace {
std::string next_for(const Finding& f) {
    if (const InstallRule* r = find_install_rule(f.rule_id)) return r->next;
    if (f.rule_id.compare(0, 5, "E-CAT") == 0)
        return "Try again later. If the problem stays, report it to the maintainer of the catalog; nothing was installed.";
    if (f.rule_id.compare(0, 5, "E-PKG") == 0 || f.rule_id.compare(0, 5, "E-ENT") == 0)
        return "Do not install this package. Report the problem to the author of the app or to the maintainer of the catalog.";
    return "Report the problem to the maintainer of the project.";
}
}  // namespace

std::string completion_text(const Plan& plan) {
    auto human = [](uint64_t bytes) {
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
        return std::string(buf);
    };
    const std::string name = neutralize(plan.app_name.empty() ? plan.app_id : plan.app_name);
    const std::string group = neutralize(plan.menu.group), title = neutralize(plan.menu.title);
    std::string t;
    switch (plan.kind) {
        case PlanKind::Install:
            t = "Done. " + name + " " + plan.to_version + " is installed. It appears in the group \"" + group + "\" as \"" + title + "\".\n";
            break;
        case PlanKind::Update:
            t = "Done. " + name + " is now version " + plan.to_version + " (it was " + plan.from_version + "). It appears in the group \"" + group + "\" as \"" + title +
                "\"; its data folder was kept.\n";
            break;
        case PlanKind::Replace:
            t = "Done. " + name + " was replaced by version " + plan.to_version + " and is now managed by the installer. Its data folder was kept.\n";
            break;
        case PlanKind::Adopt:
            t = "Done. " + name + " is now managed by the installer. Nothing else on the reader was changed.\n";
            break;
        case PlanKind::Remove: {
            t = "Done. " + name + " was removed.";
            if (plan.data && plan.data->exists && !plan.delete_data)
                t += " Its data folder " + plan.data->path + " was kept (" + std::to_string(plan.data->files) + " files, " + human(plan.data->bytes) + ").";
            size_t kept = 0;
            for (const PlanItem& i : plan.items)
                if (i.op == ItemOp::Keep) ++kept;
            if (kept) t += " " + std::to_string(kept) + " files that were changed since the install were kept.";
            t += "\n";
            break;
        }
        default: break;
    }
    if (plan.source_label.find("not from the catalog") != std::string::npos) t += "This app is " + plan.source_label + ".\n";
    t += "Before you unplug the reader, eject it safely from your desktop. A restart of the reader may be needed before the change shows.\n";
    t += "The next start of the reader may take noticeably longer than usual after a change; wait for it to finish and do not switch the reader off in between.\n";
    return t;
}

Message describe(const Finding& finding, ReaderEffect effect) {
    Message m;
    m.what = neutralize(format_finding(finding));
    m.reader_effect = reader_effect_text(effect);
    m.next = next_for(finding);
    m.rule_ids = {finding.rule_id};
    return m;
}

Message describe(const Finding& finding) {
    const InstallRule* r = find_install_rule(finding.rule_id);
    return describe(finding, r ? r->effect : ReaderEffect::Unchanged);
}

std::string Message::text() const {
    std::string t = what + "\n" + reader_effect;
    if (!next.empty()) t += "\nNext: " + next;
    return t;
}

}  // namespace install
}  // namespace pbstore
