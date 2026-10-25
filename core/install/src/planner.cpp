#include "pbstore/install/planner.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>

#include "pbstore/install/adoption.h"
#include "pbstore/install/backups.h"
#include "pbstore/install/fingerprint.h"
#include "pbstore/install/identity.h"
#include "pbstore/install/install_record.h"
#include "pbstore/install/space.h"
#include "pbstore/install/text_safety.h"
#include "pbstore/reserved_dirs.h"
#include "pbstore/semver.h"
#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

namespace {

constexpr const char* kViewPath = "system/config/desktop/view.json";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

std::string human(uint64_t bytes) {
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

std::string short_hash(const std::string& h) { return h.substr(0, 12); }

// ---- the planner's working state -------------------------------------------------------------------------------

struct Planner {
    const PlanRequest& req;
    const ReaderSnapshot& snap;
    PlanResult out;
    Plan plan;
    const Manifest* m = nullptr;
    const InstalledApp* own = nullptr;   // the record of the app this is about, if any
    std::vector<Finding>& errors() { return out.findings; }

    Planner(const PlanRequest& r, const ReaderSnapshot& s) : req(r), snap(s) {}

    void error(const char* rule, const std::string& subject, const std::string& message) { out.findings.push_back(make_error(rule, subject, message)); }
    void notice(const char* rule, const std::string& subject, const std::string& message) { out.notices.push_back(make_notice(rule, subject, message)); }

    const TargetFact* fact(const std::string& path) const {
        auto it = snap.facts.find(path);
        return it == snap.facts.end() ? nullptr : &it->second;
    }
    bool exists(const std::string& path) const {
        const TargetFact* f = fact(path);
        return f && f->kind != Kind::None;
    }

    // the id and the title of the app that a record belongs to, for messages
    static std::string describe_app(const InstalledApp& a) { return a.record.app_id + " " + a.record.version; }

    // ---- checks on the package against the reader --------------------------------------------------------------

    bool path_allowed(const std::string& p) const {
        if (!starts_with(p, "applications/") || p.size() <= 13 || !valid_relative_path(p)) return false;
        if (m->data_dir && starts_with(p, *m->data_dir + "/")) return false;
        return true;
    }

    void check_paths() {
        for (const FileEntry& f : m->files)
            if (!path_allowed(f.path)) error("E-INS-19", f.path, "the package names the path " + f.path + ", which is outside the places the installer may write");
        if (!path_allowed(m->menu_entry.path) && !m->menu_entry.path.empty())
            error("E-INS-19", m->menu_entry.path, "the menu entry names the path " + m->menu_entry.path + ", which is outside the places the installer may write");
    }

    void check_data_dir() {
        if (!m->data_dir) return;
        const std::string name = m->data_dir->size() > 7 ? m->data_dir->substr(7) : "";
        if (!starts_with(*m->data_dir, "system/") || name.empty() || name.find('/') != std::string::npos || is_reserved_data_dir(lower(name)) || lower(name) == "pbstore")
            error("E-INS-18", *m->data_dir, "the data folder " + *m->data_dir + " is a reserved system name or the installer's own folder");
    }

    void check_other_apps() {
        for (const auto& kv : snap.installed) {
            if (kv.first == req.app_id) continue;
            const InstallRecord& r = kv.second.record;
            if (kind_writes_entry() && r.menu_entry.id == m->menu_entry.id)
                error("E-INS-15", m->menu_entry.id, "the menu entry " + m->menu_entry.id + " is already used by " + describe_app(kv.second) + " (this package is " + req.app_id + ")");
            if (m->data_dir && r.data_dir && lower(*r.data_dir) == lower(*m->data_dir))
                error("E-INS-16", *m->data_dir, "the data folder " + *m->data_dir + " belongs to " + describe_app(kv.second) + " (this package is " + req.app_id + ")");
            for (const FileEntry& mine : m->files)
                for (const FileEntry& theirs : r.files)
                    if (lower(mine.path) == lower(theirs.path))
                        error("E-INS-21", mine.path, "the path " + mine.path + " belongs to " + describe_app(kv.second) + " (this package is " + req.app_id + ")");
        }
    }
    bool kind_writes_entry() const { return req.kind != PlanKind::Adopt; }

    void check_links_and_obstacles(const std::set<std::string>& owned) {
        std::set<std::string> seen_dirs;
        for (const FileEntry& f : m->files) {
            // every folder on the way: a link or a file in its place is refused
            for (std::string d = parent_path(f.path); !d.empty(); d = parent_path(d)) {
                if (!seen_dirs.insert(d).second) continue;
                const TargetFact* df = fact(d);
                if (df && df->kind == Kind::Link) error("E-INS-22", d, "a link-like object lies on the path " + d + "; the installer never follows links");
                else if (df && df->kind != Kind::None && df->kind != Kind::Dir) error("E-INS-20", d, d + " exists and is not a folder");
                // a folder that exists under another spelling of its name
                auto listing = snap.listings.find(parent_path(d));
                if (listing != snap.listings.end() && (!df || df->kind == Kind::None))
                    for (const DirEntry& e : listing->second)
                        if (lower(e.name) == lower(base_name(d)) && e.name != base_name(d))
                            error("E-INS-20", d, "the folder " + join_path(parent_path(d), e.name) + " differs from " + d + " only by letter case");
            }
            const TargetFact* ff = fact(f.path);
            if (ff && ff->kind == Kind::Link) {
                error("E-INS-22", f.path, "a link-like object lies on the path " + f.path + "; the installer never follows links");
                continue;
            }
            const bool is_owned = owned.count(f.path) > 0;
            if (!is_owned) {
                // an existing name that only differs by letter case is the same file on the reader's storage
                std::string variant;
                auto listing = snap.listings.find(parent_path(f.path));
                if (listing != snap.listings.end())
                    for (const DirEntry& e : listing->second)
                        if (lower(e.name) == lower(base_name(f.path)) && e.name != base_name(f.path)) variant = join_path(parent_path(f.path), e.name);
                if (!variant.empty())
                    error("E-INS-20", f.path, "the path " + f.path + " differs from the existing " + variant + " only by letter case");
                else if (ff && ff->kind != Kind::None && req.kind != PlanKind::Replace)
                    error("E-INS-20", f.path, "the path " + f.path + " exists and the installer did not create it");
            }
        }
    }

    void check_compatibility_and_space() {
        Compatibility c = check_compatibility(snap.identity, *m);
        for (const Finding& f : c.refusals) out.findings.push_back(f);
        for (const Requirement& r : c.requirements) {
            plan.confirmations.insert(r.confirmation);
            plan.warnings.push_back(r.finding.rule_id + ": " + r.finding.message);
        }
    }

    // ---- building the plan -------------------------------------------------------------------------------------------

    std::vector<std::string> dirs_to_create() const {
        std::vector<std::string> dirs;
        std::set<std::string> seen;
        for (const FileEntry& f : m->files) {
            std::vector<std::string> chain;
            for (std::string d = parent_path(f.path); !d.empty(); d = parent_path(d)) chain.push_back(d);
            std::reverse(chain.begin(), chain.end());
            for (const std::string& d : chain)
                if (!exists(d) && seen.insert(d).second) dirs.push_back(d);
        }
        return dirs;
    }

    // The folders of the package that hold nothing but the package's own files (below them, too), shallowest first.
    // An app that was put on the reader by hand has no record of the folders it made; these are the ones a removal
    // may tidy up when empty. A shared folder (applications, applications/icons) or one with a foreign file is not.
    std::vector<std::string> owned_dirs() const {
        std::set<std::string> package_files, package_dirs;
        for (const FileEntry& f : m->files) {
            package_files.insert(f.path);
            for (std::string d = parent_path(f.path); !d.empty(); d = parent_path(d)) package_dirs.insert(d);
        }
        std::map<std::string, bool> memo;
        std::function<bool(const std::string&)> owned = [&](const std::string& d) {
            auto known = memo.find(d);
            if (known != memo.end()) return known->second;
            memo[d] = false;   // guards against a cycle; the result is set below
            bool ok = package_dirs.count(d) != 0 && snap.listings.count(d) != 0;
            if (ok)
                for (const DirEntry& e : snap.listings.at(d)) {
                    const std::string child = d + "/" + e.name;
                    if (e.kind == Kind::File ? package_files.count(child) == 0 : (e.kind != Kind::Dir || !owned(child))) {
                        ok = false;
                        break;
                    }
                }
            memo[d] = ok;
            return ok;
        };
        std::vector<std::string> dirs;
        for (const std::string& d : package_dirs)
            if (owned(d)) dirs.push_back(d);
        std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
        return dirs;
    }

    void sort_with_launcher_last(std::vector<PlanItem>* items) const {
        std::stable_sort(items->begin(), items->end(), [](const PlanItem& a, const PlanItem& b) { return a.path < b.path; });
        std::stable_partition(items->begin(), items->end(), [&](const PlanItem& i) { return i.path != m->menu_entry.path; });
    }

    EntryValue entry_value() const { return entry_value_from_manifest(m->menu_entry); }

    // the record for the state after the operation
    InstallRecord build_record(const std::string& entry_text, const std::vector<std::string>& dirs) const {
        InstallRecord r;
        r.app_id = req.app_id;
        r.version = m->version;
        r.source = req.kind == PlanKind::Adopt ? "adopted" : req.source;
        r.package_sha256 = req.package_sha256;
        for (const FileEntry& f : m->files) r.files.push_back(f);
        std::sort(r.files.begin(), r.files.end(), [](const FileEntry& a, const FileEntry& b) { return a.path < b.path; });
        r.directories = dirs;
        r.menu_entry = m->menu_entry;
        r.entry_sha256 = sha256_hex(entry_text);
        r.data_dir = m->data_dir;
        r.written_at = req.now.iso();
        r.installed_by = req.installed_by;
        return r;
    }

    void finish(std::vector<std::string> sizes_new, std::vector<uint64_t> sizes_old) {
        plan.kind = req.kind;
        plan.app_id = req.app_id;
        plan.source = req.kind == PlanKind::Adopt ? "adopted" : req.source;
        plan.source_label = req.source_label;
        plan.catalog_generated_at = req.catalog_generated_at;
        plan.package_sha256 = req.package_sha256;
        plan.confirmations.insert(Confirmation::Plan);
        (void)sizes_new;
        (void)sizes_old;
    }

    void set_space(const std::vector<PlanItem>& items, bool old_counts) {
        SpaceInput in;
        in.cluster = snap.space.cluster_bytes ? snap.space.cluster_bytes : 4096;
        for (const PlanItem& i : items) {
            if (i.op == ItemOp::Add || i.op == ItemOp::Replace) in.new_file_sizes.push_back(i.size);
            if (old_counts && (i.op == ItemOp::Replace || i.op == ItemOp::Remove)) {
                const TargetFact* f = fact(i.path);
                in.old_file_sizes.push_back(f ? f->size : 0);
            }
        }
        if (!plan.view_new_text.empty()) {
            in.desktop_config_size = snap.view_text.size();
            in.new_desktop_config_size = plan.view_new_text.size();
        }
        in.record_size = plan.record_text.size();
        in.intent_size = 1024 + 220 * items.size();
        in.directories_to_create = plan.directories_to_create.size();
        plan.space.parts = space_needed(in);
        plan.space.needed = plan.space.parts.total();
        plan.space.free_bytes = snap.space.free_bytes;
        plan.space.cluster = in.cluster;
        if (snap.space.free_bytes < plan.space.needed)
            error("E-INS-07", "reader",
                  "not enough free space on the reader: " + human(plan.space.needed) + " are needed (" + human(plan.space.parts.margin) + " of that is a safety margin) and " +
                      human(snap.space.free_bytes) + " are free");
    }

    void seal() {
        plan.fingerprint_basis = snapshot_basis(snap, plan_target_paths(plan));
        plan.fingerprint = fingerprint_of(plan.fingerprint_basis);
        if (plan.view_new_text.empty()) {
            plan.view_old_sha256.clear();
            plan.view_new_sha256.clear();
        } else {
            plan.backup = plan_backup_from(snap.backups, req.local_now);
        }
        plan.fingerprint_basis = snapshot_basis(snap, plan_target_paths(plan));
        plan.fingerprint = fingerprint_of(plan.fingerprint_basis);
    }

    void data_dir_info(bool own_data) {
        if (!m->data_dir) return;
        DataDirPlan d;
        d.path = *m->data_dir;
        auto it = snap.data.find(d.path);
        if (it != snap.data.end()) {
            d.exists = it->second.exists;
            d.files = it->second.files;
            d.bytes = it->second.bytes;
        }
        if (own && own->record.data_dir && lower(*own->record.data_dir) == lower(d.path)) d.owner = own->record.app_id;
        plan.data = d;
        if (d.exists && d.owner.empty() && !own_data) {
            plan.confirmations.insert(Confirmation::ExistingDataFolder);
            plan.warnings.push_back("E-INS-17: the data folder " + d.path + " exists and no installed app owns it (" + std::to_string(d.files) + " files, " + human(d.bytes) +
                                    "); its contents stay untouched");
        }
    }

    // ---- the kinds ----------------------------------------------------------------------------------------------

    void entry_edit(bool entry_exists) {
        ParseResult dummy;
        const DesktopConfig& cfg = *snap.desktop;
        Change change = entry_exists ? replace_entry(m->menu_entry.id, entry_value(), m->menu_entry.group) : add_entry(m->menu_entry.id, entry_value(), m->menu_entry.group);
        EditResult er = edit(cfg, change);
        if (!er.text) {
            for (const Finding& f : er.findings) out.findings.push_back(f);
            return;
        }
        if (er.splices.empty()) return;   // the entry is already as it should be
        plan.view_old_sha256 = cfg.sha256;
        plan.view_new_text = *er.text;
        plan.view_new_sha256 = sha256_hex(plan.view_new_text);
        plan.menu.action = entry_exists ? MenuChange::Action::Replace : MenuChange::Action::Add;
        plan.menu.id = m->menu_entry.id;
        plan.menu.title = m->menu_entry.title;
        plan.menu.group = m->menu_entry.group;
        plan.menu.path = m->menu_entry.path;
    }

    std::string entry_text_after() const {
        if (!plan.view_new_text.empty()) {
            ParseResult p = parse_desktop_config(plan.view_new_text);
            if (p.config) return entry_value_text(*p.config, m->menu_entry.id);
        }
        return entry_value_text(*snap.desktop, m->menu_entry.id);
    }

    void plan_install() {
        if (own) {
            if (own->record.version == m->version) {
                notice("W-INS-01", req.app_id, req.app_id + " " + m->version + " is already installed; nothing to do");
            } else {
                error("E-INS-15", m->menu_entry.id,
                      "the menu entry " + m->menu_entry.id + " is already used by " + describe_app(*own) + ", which is the same app; use update to change its version");
            }
            return;
        }
        bool claimed_by_record = false;
        for (const auto& kv : snap.installed) claimed_by_record = claimed_by_record || kv.second.record.menu_entry.id == m->menu_entry.id;
        if (snap.desktop->has_entry(m->menu_entry.id) && !claimed_by_record)
            error("E-INS-15", m->menu_entry.id, "the menu entry " + m->menu_entry.id + " is already used by an app installed outside the installer (this package is " + req.app_id +
                                                    "); adopt or replace it instead");
        check_paths();
        check_data_dir();
        check_other_apps();
        check_links_and_obstacles({});
        check_compatibility_and_space();
        if (!out.findings.empty()) return;

        plan.to_version = m->version;
        plan.app_name = m->name;
        for (const FileEntry& f : m->files) {
            PlanItem it;
            it.op = ItemOp::Add;
            it.path = f.path;
            it.size = static_cast<uint64_t>(f.size);
            it.sha256 = f.sha256;
            plan.items.push_back(it);
        }
        sort_with_launcher_last(&plan.items);
        plan.launcher = m->menu_entry.path;
        plan.directories_to_create = dirs_to_create();
        entry_edit(false);
        if (!out.findings.empty()) return;
        data_dir_info(false);
        finish_with_record(plan.directories_to_create, RecordChange::Write, "");
        set_space(plan.items, false);
        if (!out.findings.empty()) return;
        seal();
        out.plan = plan;
    }

    void finish_with_record(const std::vector<std::string>& dirs, RecordChange change, const std::string& old_record_sha) {
        finish({}, {});
        const std::string entry_text = entry_text_after();
        plan.record_change = change;
        plan.record_text = write_install_record(build_record(entry_text, dirs));
        plan.record_new_sha256 = sha256_hex(plan.record_text);
        plan.record_old_sha256 = old_record_sha;
    }

    std::string old_record_sha() const {
        // the hash of the record text on the reader is not part of the snapshot; the fingerprint covers the record path
        auto it = snap.facts.find(record_path(req.app_id));
        return it == snap.facts.end() ? std::string() : it->second.sha256;
    }

    void plan_update(bool replace_outside) {
        std::map<std::string, FileEntry> old_files;
        std::set<std::string> owned;
        if (own) {
            for (const FileEntry& f : own->record.files) {
                old_files[f.path] = f;
                owned.insert(f.path);
            }
        }
        check_paths();
        check_data_dir();
        check_other_apps();
        check_links_and_obstacles(owned);
        check_compatibility_and_space();
        if (!out.findings.empty()) return;

        bool edited = false;
        std::vector<PlanItem> changes, removes, keeps;
        std::set<std::string> new_paths;
        for (const FileEntry& f : m->files) {
            new_paths.insert(f.path);
            const TargetFact* now = fact(f.path);
            const bool present = now && now->kind == Kind::File;
            PlanItem it;
            it.path = f.path;
            it.size = static_cast<uint64_t>(f.size);
            it.sha256 = f.sha256;
            if (!present) {
                it.op = ItemOp::Add;
                changes.push_back(it);
                continue;
            }
            it.old_sha256 = now->sha256;
            if (now->sha256 == f.sha256) {   // already as it should be
                it.op = ItemOp::Keep;
                it.reason = "already the same";
                keeps.push_back(it);
                continue;
            }
            it.op = ItemOp::Replace;
            if (replace_outside) {
                it.reason = "differs from the package";
            } else {
                auto o = old_files.find(f.path);
                if (o != old_files.end() && o->second.sha256 != now->sha256) {
                    it.reason = "changed since the install";
                    edited = true;
                }
            }
            changes.push_back(it);
        }
        for (const auto& kv : old_files) {
            if (new_paths.count(kv.first)) continue;
            const TargetFact* now = fact(kv.first);
            if (!now || now->kind != Kind::File) continue;   // already gone
            PlanItem it;
            it.op = ItemOp::Remove;
            it.path = kv.first;
            it.size = now->size;
            it.sha256 = kv.second.sha256;
            it.old_sha256 = now->sha256;
            if (now->sha256 != kv.second.sha256) {
                it.reason = "changed since the install";
                edited = true;
            }
            removes.push_back(it);
        }
        sort_with_launcher_last(&changes);
        std::sort(removes.begin(), removes.end(), [](const PlanItem& a, const PlanItem& b) { return a.path < b.path; });
        std::sort(keeps.begin(), keeps.end(), [](const PlanItem& a, const PlanItem& b) { return a.path < b.path; });
        plan.items = changes;
        plan.items.insert(plan.items.end(), removes.begin(), removes.end());
        plan.items.insert(plan.items.end(), keeps.begin(), keeps.end());
        plan.launcher = m->menu_entry.path;
        plan.directories_to_create = dirs_to_create();
        plan.to_version = m->version;
        plan.app_name = m->name;
        if (own) plan.from_version = own->record.version;
        if (edited) plan.confirmations.insert(Confirmation::EditedFiles);
        if (own && own->record.source == "local" && req.source == "catalog")
            plan.warnings.push_back("The installed copy came from a local file (not from the catalog); after this update it is managed as an app from the catalog.");
        if (replace_outside) plan.confirmations.insert(Confirmation::ReplaceOutside);

        entry_edit(snap.desktop->has_entry(m->menu_entry.id));
        if (!out.findings.empty()) return;
        data_dir_info(own != nullptr);

        // folders the old version made that the new version no longer needs go if they are empty, deepest first
        std::set<std::string> needed;
        for (const FileEntry& f : m->files)
            for (std::string d = parent_path(f.path); !d.empty(); d = parent_path(d)) needed.insert(d);
        std::set<std::string> dirs;
        if (own)
            for (const std::string& d : own->record.directories) {
                if (needed.count(d)) dirs.insert(d);
                else plan.directories_to_remove.push_back(d);
            }
        std::sort(plan.directories_to_remove.begin(), plan.directories_to_remove.end(),
                  [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() > b.size() : a < b; });
        dirs.insert(plan.directories_to_create.begin(), plan.directories_to_create.end());
        if (!own) {   // replacing an app that was put there by hand: its own folders count as the installer's from now on
            const std::vector<std::string> mine = owned_dirs();
            dirs.insert(mine.begin(), mine.end());
        }
        finish_with_record(std::vector<std::string>(dirs.begin(), dirs.end()), own ? RecordChange::Replace : RecordChange::Write, own ? old_record_sha() : "");
        set_space(plan.items, true);
        if (!out.findings.empty()) return;
        // nothing at all to do: same version, every file the same, the entry as it should be
        bool anything = !plan.view_new_text.empty();
        for (const PlanItem& i : plan.items) anything = anything || i.op != ItemOp::Keep;
        if (!anything && own && own->record.version == m->version) {
            notice("W-INS-01", req.app_id, req.app_id + " " + m->version + " is already installed; nothing to do");
            return;
        }
        seal();
        out.plan = plan;
    }

    void plan_remove() {
        const InstallRecord& r = own->record;
        plan.app_name = r.menu_entry.title;
        plan.from_version = r.version;
        std::vector<PlanItem> removes, keeps;
        for (const FileEntry& f : r.files) {
            const TargetFact* now = fact(f.path);
            if (!now || now->kind == Kind::None) continue;   // already gone
            PlanItem it;
            it.path = f.path;
            it.size = now->size;
            it.sha256 = f.sha256;
            if (now->kind == Kind::File && now->sha256 == f.sha256) {
                it.op = ItemOp::Remove;
                it.old_sha256 = now->sha256;
                removes.push_back(it);
            } else {
                it.op = ItemOp::Keep;
                it.old_sha256 = now->sha256;
                it.reason = "changed since the install, stays";
                keeps.push_back(it);
            }
        }
        std::sort(removes.begin(), removes.end(), [](const PlanItem& a, const PlanItem& b) { return a.path < b.path; });
        std::sort(keeps.begin(), keeps.end(), [](const PlanItem& a, const PlanItem& b) { return a.path < b.path; });
        plan.items = removes;
        plan.items.insert(plan.items.end(), keeps.begin(), keeps.end());
        // folders the installer created, deepest first; the executor removes the ones that are empty
        std::vector<std::string> dirs = r.directories;
        std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() > b.size() : a < b; });
        plan.directories_to_remove = dirs;

        const DesktopConfig& cfg = *snap.desktop;
        if (cfg.has_entry(r.menu_entry.id)) {
            if (sha256_hex(entry_value_text(cfg, r.menu_entry.id)) != r.entry_sha256)
                notice("W-INS-02", r.menu_entry.id, "the menu entry " + r.menu_entry.id + " was changed since the install; it is removed anyway");
            EditResult er = edit(cfg, remove_entry(r.menu_entry.id));
            if (!er.text) {
                for (const Finding& f : er.findings) out.findings.push_back(f);
                return;
            }
            plan.view_old_sha256 = cfg.sha256;
            plan.view_new_text = *er.text;
            plan.view_new_sha256 = sha256_hex(plan.view_new_text);
            plan.menu.action = MenuChange::Action::Remove;
            plan.menu.id = r.menu_entry.id;
            plan.menu.title = r.menu_entry.title;
            plan.menu.group = r.menu_entry.group;
            plan.menu.path = r.menu_entry.path;
        } else {
            notice("W-INS-02", r.menu_entry.id, "the menu entry " + r.menu_entry.id + " is no longer in the desktop configuration");
        }
        if (r.data_dir) {
            DataDirPlan d;
            d.path = *r.data_dir;
            d.owner = req.app_id;
            auto it = snap.data.find(d.path);
            if (it != snap.data.end()) {
                d.exists = it->second.exists;
                d.files = it->second.files;
                d.bytes = it->second.bytes;
            }
            plan.data = d;
        }
        finish({}, {});
        plan.record_change = RecordChange::Delete;
        plan.record_old_sha256 = old_record_sha();
        set_space(plan.items, true);
        if (!out.findings.empty()) return;
        seal();
        out.plan = plan;
    }

    void plan_adopt(const AdoptionCheck& check) {
        // adoption writes only the record
        if (!check.adoptable) {
            error("E-INS-20", req.app_id,
                  "the files of " + req.app_id + " on the reader are not the same as the catalog package, so it cannot be adopted (" + check.text() +
                      "); replace it instead to get the package's files");
            return;
        }
        plan.to_version = m->version;
        plan.app_name = m->name;
        plan.launcher = m->menu_entry.path;
        for (const FileEntry& f : m->files) {
            PlanItem it;
            it.op = ItemOp::Keep;
            it.path = f.path;
            it.size = static_cast<uint64_t>(f.size);
            it.sha256 = f.sha256;
            it.old_sha256 = f.sha256;
            it.reason = "equal to the package";
            plan.items.push_back(it);
        }
        std::sort(plan.items.begin(), plan.items.end(), [](const PlanItem& a, const PlanItem& b) { return a.path < b.path; });
        data_dir_info(true);
        finish_with_record(owned_dirs(), RecordChange::Write, "");
        set_space({}, false);
        if (!out.findings.empty()) return;
        seal();
        out.plan = plan;
    }
};

bool snapshot_problems(const ReaderSnapshot& snap, PlanResult* out) {
    bool bad = false;
    for (const Finding& f : snap.findings) {
        out->findings.push_back(f);
        bad = true;
    }
    if (!snap.desktop && !bad) {
        out->findings.push_back(make_error("E-INS-12", kViewPath, "the desktop configuration cannot be used"));
        bad = true;
    }
    if (snap.journal_present) {
        out->findings.push_back(make_error("E-INS-23", "reader", "an earlier operation on this reader was interrupted and has to be repaired first"));
        bad = true;
    }
    return bad;
}

}  // namespace

PlanResult make_plan(const PlanRequest& req, const ReaderSnapshot& snap) {
    Planner p(req, snap);
    if (snapshot_problems(snap, &p.out)) return p.out;
    auto rec = snap.installed.find(req.app_id);
    p.own = rec == snap.installed.end() ? nullptr : &rec->second;
    p.m = req.manifest;
    if (req.kind != PlanKind::Remove && !p.m) {
        p.error("E-INS-28", req.app_id, "no package is available for " + req.app_id);
        return p.out;
    }
    switch (req.kind) {
        case PlanKind::Install:
            p.plan_install();
            break;
        case PlanKind::Update:
            if (!p.own) {
                if (snap.desktop->has_entry(p.m->menu_entry.id))
                    p.error("E-INS-31", req.app_id, req.app_id + " has no install record, so it cannot be updated; adopt it (if its files equal the package) or replace it");
                else
                    p.error("E-INS-30", req.app_id, req.app_id + " is not installed");
                break;
            }
            {
                auto offered = parse_semver(p.m->version);
                auto have = parse_semver(p.own->record.version);
                if (offered && have && compare(*offered, *have) <= 0) {
                    p.notice("W-INS-01", req.app_id, req.app_id + " " + p.own->record.version + " is installed and " + p.m->version + " is offered; nothing to do");
                    break;
                }
            }
            p.plan_update(false);
            break;
        case PlanKind::Replace:
            if (p.own) {
                p.error("E-INS-15", p.m->menu_entry.id, req.app_id + " was installed by the installer; use update instead of replace");
                break;
            }
            if (!snap.desktop->has_entry(p.m->menu_entry.id)) {
                p.error("E-INS-30", req.app_id, req.app_id + " is not installed outside the installer; use install");
                break;
            }
            p.plan_update(true);
            break;
        case PlanKind::Remove:
            if (!p.own) {
                // the app may be on the reader without a record
                bool by_hand = false;
                if (snap.desktop) {
                    const json::Value* apps = snap.desktop->tree.find("applications");
                    if (apps)
                        for (const auto& kv : apps->object)
                            if (kv.first.compare(0, 2, "U_") == 0 && lower(kv.first) == lower("U_" + req.app_id)) by_hand = true;
                }
                if (by_hand) p.error("E-INS-31", req.app_id, req.app_id + " has no install record, so it cannot be removed; adopt it (if its files equal the catalog package) or replace it first");
                else p.error("E-INS-30", req.app_id, req.app_id + " is not installed");
                break;
            }
            p.plan_remove();
            break;
        case PlanKind::Adopt: {
            if (p.own) {
                p.notice("W-INS-01", req.app_id, req.app_id + " is already known to the installer; nothing to do");
                break;
            }
            if (!snap.desktop->has_entry(p.m->menu_entry.id)) {
                p.error("E-INS-30", req.app_id, req.app_id + " is not on the reader");
                break;
            }
            p.check_paths();
            p.check_data_dir();
            p.check_other_apps();
            p.check_compatibility_and_space();
            if (!p.out.findings.empty()) break;
            p.plan_adopt(compare_to_package(snap, *p.m));
            break;
        }
        default:
            p.error("E-INS-19", req.app_id, "this kind of plan is not made by the planner");
    }
    return p.out;
}

// ---- the one text of a plan ---------------------------------------------------------------------------------------------

std::string render_plan(const Plan& plan) {
    std::string t;
    const bool local = plan.source_label.find("not from the catalog") != std::string::npos;
    if (local) t += "NOTE: " + plan.source_label + "\n";
    const char* verb = "";
    switch (plan.kind) {
        case PlanKind::Install: verb = "Install"; break;
        case PlanKind::Update: verb = "Update"; break;
        case PlanKind::Remove: verb = "Remove"; break;
        case PlanKind::Adopt: verb = "Adopt"; break;
        case PlanKind::Replace: verb = "Replace"; break;
        case PlanKind::Repair: verb = "Repair"; break;
        case PlanKind::DeleteData: verb = "Delete the data folder of"; break;
    }
    if (plan.kind == PlanKind::DeleteData && plan.data) {
        t += "Plan: delete the data folder " + plan.data->path + " of " + plan.app_id + "\n";
        t += "It holds " + std::to_string(plan.data->files) + " files (" + human(plan.data->bytes) + "). Nothing else is touched. This cannot be undone.\n";
        t += "You must confirm twice, with different answers: the name of the folder, then the number of files.\n";
        return neutralize(t, true);
    }
    t += std::string("Plan: ") + verb + " " + (plan.app_name.empty() ? plan.app_id : plan.app_name) + " (" + plan.app_id + ")";
    if (!plan.from_version.empty() && !plan.to_version.empty()) t += ", " + plan.from_version + " -> " + plan.to_version;
    else if (!plan.to_version.empty()) t += ", version " + plan.to_version;
    else if (!plan.from_version.empty()) t += ", version " + plan.from_version;
    t += "\n";
    t += "Source: " + (plan.source_label.empty() ? plan.source : plan.source_label);
    if (!plan.catalog_generated_at.empty()) t += " (catalog generated " + plan.catalog_generated_at.substr(0, 10) + ")";
    t += "\n";

    if (!plan.items.empty()) {
        uint64_t bytes = 0;
        size_t changing = 0;
        for (const PlanItem& i : plan.items)
            if (i.op != ItemOp::Keep) {
                ++changing;
                if (i.op != ItemOp::Remove) bytes += i.size;
            }
        t += "Files (" + std::to_string(changing) + " to change, " + human(bytes) + " to write):\n";
        for (const PlanItem& i : plan.items) {
            const char* op = i.op == ItemOp::Add ? "add    " : i.op == ItemOp::Replace ? "replace" : i.op == ItemOp::Remove ? "remove " : "keep   ";
            t += std::string("  ") + op + " " + i.path + "  " + human(i.size) + "  sha256 " + short_hash(i.sha256);
            if (!i.reason.empty()) t += "  (" + i.reason + ")";
            t += "\n";
        }
    }
    if (!plan.directories_to_create.empty()) {
        t += "Folders to create:";
        for (const std::string& d : plan.directories_to_create) t += " " + d;
        t += "\n";
    }
    if (!plan.directories_to_remove.empty()) {
        t += "Folders removed if they are empty:";
        for (const std::string& d : plan.directories_to_remove) t += " " + d;
        t += "\n";
    }
    switch (plan.menu.action) {
        case MenuChange::Action::Add: t += "Menu entry: add \"" + plan.menu.title + "\" (" + plan.menu.id + ") to the group \"" + plan.menu.group + "\"\n"; break;
        case MenuChange::Action::Replace: t += "Menu entry: change \"" + plan.menu.title + "\" (" + plan.menu.id + "), group \"" + plan.menu.group + "\"\n"; break;
        case MenuChange::Action::Remove: t += "Menu entry: remove \"" + plan.menu.title + "\" (" + plan.menu.id + ") from the group \"" + plan.menu.group + "\"\n"; break;
        case MenuChange::Action::None: t += "Menu entry: no change\n"; break;
    }
    if (plan.record_change != RecordChange::None) {
        const char* what = plan.record_change == RecordChange::Write ? "write" : plan.record_change == RecordChange::Replace ? "replace" : "delete";
        t += std::string("Install record: ") + what + " system/pbstore/installed/" + plan.app_id + ".json\n";
    }
    if (plan.data) {
        t += "Data folder: " + plan.data->path;
        if (plan.kind == PlanKind::Remove) {
            if (!plan.data->exists) t += ", does not exist\n";
            else if (plan.delete_data)
                t += " (" + std::to_string(plan.data->files) + " files, " + human(plan.data->bytes) + ") will be deleted after two further confirmations, once the app is removed\n";
            else
                t += ", kept (" + std::to_string(plan.data->files) + " files, " + human(plan.data->bytes) + "); it is deleted only if you ask for that separately\n";
        } else if (plan.data->exists) {
            t += ": exists (" + std::to_string(plan.data->files) + " files, " + human(plan.data->bytes) + "); its contents stay untouched\n";
        } else {
            t += ": none yet; the installer never creates or fills it\n";
        }
    } else if (plan.kind != PlanKind::Remove) {
        t += "Data folder: none\n";
    }
    if (!plan.backup.name.empty()) {
        t += "Desktop configuration: a backup is made first as system/pbstore/backups/" + plan.backup.name + "\n";
        if (!plan.backup.delete_after_commit.empty()) {
            t += "  older backups of the same day, deleted after the change:";
            for (const std::string& b : plan.backup.delete_after_commit) t += " " + b;
            t += "\n";
        }
    }
    if (plan.space.needed > 0)
        t += "Space: " + human(plan.space.needed) + " needed (" + human(plan.space.parts.margin) + " of it a safety margin), " + human(plan.space.free_bytes) + " free\n";
    if (!plan.warnings.empty()) {
        t += "Warnings:\n";
        for (const std::string& w : plan.warnings) t += "  - " + w + "\n";
    }
    t += "You must confirm:";
    bool first = true;
    for (Confirmation c : plan.confirmations) {
        const char* what = "";
        switch (c) {
            case Confirmation::Plan: what = "the plan above"; break;
            case Confirmation::EditedFiles: what = "that files changed since the install are replaced or removed (E-INS-32)"; break;
            case Confirmation::UnknownFirmware: what = "that the firmware version of the reader is unknown (E-INS-11)"; break;
            case Confirmation::UnknownModel: what = "that the model of the reader is unknown (E-INS-09)"; break;
            case Confirmation::ExistingDataFolder: what = "that the existing data folder is used as it is (E-INS-17)"; break;
            case Confirmation::ReplaceOutside: what = "that an app installed outside the installer is replaced (E-INS-38)"; break;
            case Confirmation::Repair: what = "the repair of an interrupted operation (E-INS-23)"; break;
            case Confirmation::DeleteData1: what = "the name of the data folder to delete"; break;
            case Confirmation::DeleteData2: what = "the number of files to delete"; break;
        }
        t += std::string(first ? " " : "; ") + what;
        first = false;
    }
    t += "\n";
    return neutralize(t, true);
}

}  // namespace install
}  // namespace pbstore
