#pragma once
// Test helper: hand-made plans and matching package content for the executor tests, built from the lower
// modules (desktop configuration editor, install record, fingerprint, space) and not from the planner, so
// the executor tests do not depend on it.

#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "crash_fs.h"
#include "fake_reader.h"
#include "fat_rules_fs.h"
#include "fixed_clock.h"
#include "forwarding_fs.h"
#include "pbstore/install/backups.h"
#include "pbstore/install/content.h"
#include "pbstore/install/desktop_config.h"
#include "pbstore/install/executor.h"
#include "pbstore/install/fingerprint.h"
#include "pbstore/install/install_record.h"
#include "pbstore/install/journal.h"
#include "pbstore/install/plan.h"
#include "pbstore/install/planner.h"
#include "pbstore/install/reader_state.h"
#include "pbstore/install/recovery.h"
#include "pbstore/install/space.h"
#include "sample_app.h"
#include "tree_compare.h"

namespace pbtest {

using namespace pbstore::install;

struct Built {
    Plan plan;
    MapContent content;
    std::string package_zip;
};

// A sample app with files in a nested folder as well, so directories have to be created.
inline SampleApp nested_sample_app(const std::string& id = "testapp", const std::string& version = "1.2.0",
                                   const std::string& menu_id = "U_TestApp", unsigned seed = 1) {
    SampleApp app = make_sample_app(id, version, menu_id, "system/" + id, seed);
    app.files.push_back(file("applications/" + id + "/lib/core.bin", pseudo_bytes(9000, seed + 30)));
    app.files.push_back(file("applications/" + id + "/lib/extra/data.txt", "hello " + id + " " + version + "\n"));
    app.manifest.set("files", files_value(app.files));
    return app;
}

// Version 1.1.0 of the nested sample app: the main files change, one file is dropped, one is added.
inline SampleApp nested_sample_app_v2(const std::string& id = "testapp", const std::string& menu_id = "U_TestApp", const std::string& version = "1.1.0") {
    SampleApp app = nested_sample_app(id, version, menu_id, 77);
    app.files.erase(std::remove_if(app.files.begin(), app.files.end(), [&](const ZEntry& f) { return f.name == "applications/" + id + "/lib/extra/data.txt"; }), app.files.end());
    app.files.push_back(file("applications/" + id + "/new.bin", pseudo_bytes(2000, 5)));
    app.manifest.set("files", files_value(app.files));
    return app;
}

inline std::string reader_view_text(ReaderFs& fs) {
    std::string t;
    fs.read_file("system/config/desktop/view.json", 8u << 20, &t);
    return t;
}

// The plan an installation of `app` onto the reader as it is in `fs` would have.
inline Built make_install_plan(ReaderFs& fs, const SampleApp& app, const std::string& source = "catalog") {
    using pbstore::json::Value;
    Built b;
    Plan& p = b.plan;
    FixedClock clock;
    const Value* menu = app.manifest.find("menu_entry");
    p.kind = PlanKind::Install;
    p.app_id = app.id;
    p.app_name = app.manifest.get_string("name").value();
    p.to_version = app.manifest.get_string("version").value();
    p.source = source;
    p.source_label = source == "catalog" ? "catalog" : "not from the catalog, not signed by the maintainer";
    p.catalog_generated_at = "2026-10-24T12:00:00Z";
    p.package_sha256 = pbstore::sha256_hex(app.id + p.to_version);

    // files: sorted by path, the launcher last
    std::vector<ZEntry> files = app.files;
    std::sort(files.begin(), files.end(), [](const ZEntry& a, const ZEntry& c) { return a.name < c.name; });
    p.launcher = menu->get_string("path").value();
    std::stable_partition(files.begin(), files.end(), [&](const ZEntry& f) { return f.name != p.launcher; });
    std::set<std::string> dirs_seen;
    for (const ZEntry& f : files) {
        PlanItem it;
        it.op = ItemOp::Add;
        it.path = f.name;
        it.size = f.data.size();
        it.sha256 = pbstore::sha256_hex(f.data);
        p.items.push_back(it);
        b.content.files[f.name] = f.data;
        // directories below applications/ that do not exist yet, parents first
        std::string dir;
        const std::string parent = parent_path(f.name);
        size_t start = 0;
        while (start < parent.size()) {
            const size_t slash = parent.find('/', start);
            dir = parent.substr(0, slash == std::string::npos ? std::string::npos : slash);
            if (!fs.stat(dir).exists() && dirs_seen.insert(dir).second) p.directories_to_create.push_back(dir);
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
    }

    // the desktop configuration
    const std::string old_view = reader_view_text(fs);
    ParseResult parsed = parse_desktop_config(old_view);
    pbstore::MenuEntry me;
    me.id = menu->get_string("id").value();
    me.title = menu->get_string("title").value();
    me.path = menu->get_string("path").value();
    me.icon = menu->get_string("icon").value();
    me.focused_icon = menu->get_string("focused_icon").value();
    me.group = menu->get_string("group").value();
    EditResult er = edit(*parsed.config, add_entry(me.id, entry_value_from_manifest(me), me.group));
    p.view_old_sha256 = parsed.config->sha256;
    p.view_new_text = *er.text;
    p.view_new_sha256 = pbstore::sha256_hex(p.view_new_text);
    p.menu.action = MenuChange::Action::Add;
    p.menu.id = me.id;
    p.menu.title = me.title;
    p.menu.group = me.group;
    p.menu.path = me.path;
    ParseResult after = parse_desktop_config(p.view_new_text);

    // the record
    InstallRecord r;
    r.app_id = app.id;
    r.version = p.to_version;
    r.source = source;
    r.package_sha256 = p.package_sha256;
    for (const PlanItem& it : p.items) r.files.push_back({it.path, static_cast<int64_t>(it.size), it.sha256});
    r.directories = p.directories_to_create;
    r.menu_entry = me;
    r.entry_sha256 = pbstore::sha256_hex(entry_value_text(*after.config, me.id));
    const Value* dd = app.manifest.find("data_dir");
    if (dd && dd->is_string()) r.data_dir = dd->string;
    r.written_at = clock.now().iso();
    r.installed_by = "pbstore-install 0.2.0";
    p.record_change = RecordChange::Write;
    p.record_text = write_install_record(r);
    p.record_new_sha256 = pbstore::sha256_hex(p.record_text);
    if (r.data_dir) {
        DataDirPlan d;
        d.path = *r.data_dir;
        d.exists = fs.stat(d.path).exists();
        p.data = d;
    }

    p.backup = plan_backup(fs, clock.local_now());

    SpaceInput si;
    const Space sp = fs.space();
    si.cluster = sp.cluster_bytes;
    for (const PlanItem& it : p.items) si.new_file_sizes.push_back(it.size);
    si.desktop_config_size = old_view.size();
    si.new_desktop_config_size = p.view_new_text.size();
    si.record_size = p.record_text.size();
    si.intent_size = 4096;
    si.directories_to_create = p.directories_to_create.size();
    p.space.parts = space_needed(si);
    p.space.needed = p.space.parts.total();
    p.space.free_bytes = sp.free_bytes;
    p.space.cluster = sp.cluster_bytes;

    p.confirmations.insert(Confirmation::Plan);
    p.fingerprint_basis = read_basis(fs, plan_target_paths(p));
    p.fingerprint = fingerprint_of(p.fingerprint_basis);
    b.package_zip = build_package(app);
    return b;
}

// The reader as it is after `app` was installed on it by the installer (files, entry, record).
inline FakeReader with_installed(const FakeReader& before, const SampleApp& app) {
    CrashFs mem;
    before.load_into(mem);
    FatRulesFs fat(mem);
    Built b = make_install_plan(fat, app);
    CancelToken cancel;
    ExecResult r = execute(fat, b.plan, b.content, b.plan.confirmations, [](const Progress&) {}, cancel);
    if (!r.ok()) throw std::runtime_error("fixture: the installation did not succeed");
    FakeReader after = before;
    after.assign_tree(mem.tree());
    return after;
}

// A manifest as the planner receives it, from a sample app.
inline pbstore::Manifest manifest_of(const SampleApp& app) {
    pbstore::ManifestParseResult r = pbstore::parse_manifest(pbstore::json::write(app.manifest));
    if (!r.manifest) throw std::runtime_error("fixture: the sample manifest is not valid");
    return *r.manifest;
}

// The plan the real planner makes for `app` (kind install, update, remove, adopt or replace) on the reader as it
// is in `fs`; the content comes from the sample app. Throws if there is no plan.
inline Built make_planned(ReaderFs& fs, const SampleApp& app, PlanKind kind = PlanKind::Install, const std::string& source = "catalog") {
    Built b;
    pbstore::Manifest m = manifest_of(app);
    ScanRequest sr;
    sr.app_id = app.id;
    sr.manifest = kind == PlanKind::Remove ? nullptr : &m;
    ReaderSnapshot snap = scan(fs, Identity{std::string("1030"), std::string("6.11.1437")}, sr);
    PlanRequest pr;
    pr.kind = kind;
    pr.app_id = app.id;
    pr.manifest = kind == PlanKind::Remove ? nullptr : &m;
    pr.package_sha256 = pbstore::sha256_hex(app.id + m.version);
    pr.source = source;
    pr.source_label = source == "catalog" ? "catalog" : "not from the catalog, not signed by the maintainer";
    pr.catalog_generated_at = "2026-10-24T12:00:00Z";
    FixedClock clock;
    pr.now = clock.now();
    pr.local_now = clock.local_now();
    pr.installed_by = "pbstore-install 0.2.0";
    PlanResult r = make_plan(pr, snap);
    if (!r.plan) throw std::runtime_error("fixture: the planner made no plan: " + (r.findings.empty() ? std::string("(no finding)") : r.findings[0].rule_id + " " + r.findings[0].message));
    b.plan = *r.plan;
    for (const ZEntry& f : app.files) b.content.files[f.name] = f.data;
    b.package_zip = build_package(app);
    return b;
}

inline Confirmations given_for(const Plan& plan) { return plan.confirmations; }

inline ExecResult run_execute(ReaderFs& fs, Built& b, CancelToken* cancel = nullptr, const ProgressFn& progress = nullptr) {
    CancelToken local;
    return execute(fs, b.plan, b.content, given_for(b.plan), progress ? progress : ProgressFn([](const Progress&) {}), cancel ? *cancel : local);
}

// The volume under test: the fake reader in memory behind FAT rules.
struct Volume {
    CrashFs mem;
    FatRulesFs fat{mem};
    explicit Volume(const FakeReader& reader) { reader.load_into(mem); }
    Volume(const Volume&) = delete;
};

// Everything outside the installer's own area, for "the live area is unchanged".
inline Snapshot without_installer_area(Snapshot s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](const TreeItem& i) { return i.path == "system/pbstore" || i.path.compare(0, 15, "system/pbstore/") == 0; }), s.end());
    return s;
}
inline bool installer_area_has_files(const Snapshot& s) {
    for (const TreeItem& i : s)
        if (i.kind == 'f' && i.path.compare(0, 15, "system/pbstore/") == 0) return true;
    return false;
}

// A forwarding double that records every call (mutating calls and reads) for order and access checks.
class TraceFs : public ForwardingFs {
public:
    struct Op {
        std::string name;   // create, rename, remove_file, make_dir, remove_dir, flush_dir, flush_volume, read, stat, list
        std::string a, b;
        bool mutating = false;
    };
    using ForwardingFs::ForwardingFs;
    std::vector<Op> ops;

    pbstore::install::Stat stat(const Path& p) override { ops.push_back({"stat", p, "", false}); return ForwardingFs::stat(p); }
    std::vector<pbstore::install::DirEntry> list(const Path& d) override { ops.push_back({"list", d, "", false}); return ForwardingFs::list(d); }
    bool read_file(const Path& p, uint64_t max, std::string* out) override { ops.push_back({"read", p, "", false}); return ForwardingFs::read_file(p, max, out); }
    bool create_exclusive(const Path& p, const std::string& data, bool flush) override { ops.push_back({"create", p, "", true}); return ForwardingFs::create_exclusive(p, data, flush); }
    bool rename_free(const Path& a, const Path& b) override { ops.push_back({"rename", a, b, true}); return ForwardingFs::rename_free(a, b); }
    bool remove_file(const Path& p) override { ops.push_back({"remove_file", p, "", true}); return ForwardingFs::remove_file(p); }
    bool make_dir(const Path& p) override { ops.push_back({"make_dir", p, "", true}); return ForwardingFs::make_dir(p); }
    bool remove_dir(const Path& p) override { ops.push_back({"remove_dir", p, "", true}); return ForwardingFs::remove_dir(p); }
    bool flush_dir(const Path& d) override { ops.push_back({"flush_dir", d, "", true}); return ForwardingFs::flush_dir(d); }
    bool flush_volume() override { ops.push_back({"flush_volume", "", "", true}); return ForwardingFs::flush_volume(); }
};

inline bool in_installer_area(const std::string& p) { return p == "system/pbstore" || p.compare(0, 15, "system/pbstore/") == 0; }

// How many mutating calls the whole execution of the plan makes on a fresh copy of the reader.
inline int count_mutating_calls(const FakeReader& reader, const std::function<Built(ReaderFs&)>& make) {
    Volume v(reader);
    Built b = make(v.fat);
    v.mem.reboot();   // resets the call counter
    run_execute(v.fat, b);
    return v.mem.mutating_calls();
}

}  // namespace pbtest
