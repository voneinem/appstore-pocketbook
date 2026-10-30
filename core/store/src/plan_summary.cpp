#include "pbstore/store/plan_summary.h"

#include <cstdio>

#include "pbstore/store/store_text.h"

namespace pbstore {
namespace store {

namespace {

std::string megabytes(uint64_t bytes) {
    if (bytes < 100 * 1024) return "less than 0.1 MB";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

std::string files(uint64_t n) { return std::to_string(n) + (n == 1 ? " file" : " files"); }

}  // namespace

std::string plain_plan_summary(const install::Plan& plan) {
    using install::PlanKind;
    const std::string name = store_text(plan.app_name.empty() ? plan.app_id : plan.app_name, kMaxNameChars);
    std::string s;
    auto line = [&s](const std::string& text) { s += (s.empty() ? "" : "\n") + text; };

    if (plan.kind == PlanKind::DeleteData) {
        line("Delete the data of " + name);
        if (plan.data)
            line(files(plan.data->files) + " (" + megabytes(plan.data->bytes) + ") in " + store_text(plan.data->path, 120) + " will be deleted.");
        line("This cannot be undone. The app itself is not touched.");
        return s;
    }

    uint64_t to_write = 0, to_free = 0, edited = 0;
    for (const install::PlanItem& i : plan.items) {
        if (i.op == install::ItemOp::Add || i.op == install::ItemOp::Replace) to_write += i.size;
        if (i.op == install::ItemOp::Remove) to_free += i.size;
        if (i.reason.find("changed since") != std::string::npos) ++edited;
    }
    const std::string version = store_text(plan.to_version.empty() ? plan.from_version : plan.to_version, kMaxNameChars);
    const std::string title = store_text(plan.menu.title.empty() ? plan.app_name : plan.menu.title, kMaxNameChars);

    switch (plan.kind) {
        case PlanKind::Install: line("Install " + name + (version.empty() ? "" : " " + version)); break;
        case PlanKind::Update:
            line("Update " + name + (plan.from_version.empty() || plan.to_version.empty()
                                         ? (version.empty() ? "" : " to " + version)
                                         : " from " + store_text(plan.from_version, kMaxNameChars) + " to " + version));
            break;
        case PlanKind::Remove: line("Remove " + name + (version.empty() ? "" : " " + version)); break;
        case PlanKind::Adopt: line("Let PB Store manage " + name + (version.empty() ? "" : " " + version)); break;
        case PlanKind::Replace: line("Replace " + name + " with the catalog's version" + (version.empty() ? "" : " " + version)); break;
        case PlanKind::Repair: line("Finish or undo an interrupted operation"); break;
        case PlanKind::DeleteData: break;
    }

    if (plan.kind != PlanKind::Remove && plan.kind != PlanKind::Repair) {
        if (plan.source_label.find("not from the catalog") != std::string::npos)
            line("Not from the catalog and not signed by the maintainer: continue only if you trust where the file came from.");
        else
            line("From the PB Store catalog.");
    }
    if (plan.kind == PlanKind::Adopt) line("No files change.");
    else if (plan.kind == PlanKind::Replace) line("The files that were put there by hand are replaced by those of the catalog.");
    if (to_write > 0) line("Needs about " + megabytes(to_write) + " on the reader.");
    else if (plan.kind == PlanKind::Remove && to_free > 0) line("Frees about " + megabytes(to_free) + " on the reader.");

    switch (plan.menu.action) {
        case install::MenuChange::Action::Add: line("It appears as \"" + title + "\" in the menu."); break;
        case install::MenuChange::Action::Remove: line("It disappears from the menu."); break;
        default: break;
    }
    if (edited > 0) line(files(edited) + " of this app " + (edited == 1 ? "was" : "were") + " changed since it was installed (see the details).");
    if (plan.data && plan.data->exists) {
        if (plan.kind == PlanKind::Remove && plan.delete_data) line("Your data of this app is deleted afterwards, after two more questions.");
        else if (plan.kind == PlanKind::Remove) line("Your data of this app is kept. You can delete it afterwards.");
        else line("Your data of this app is kept.");
    }
    return s;
}

}  // namespace store
}  // namespace pbstore
