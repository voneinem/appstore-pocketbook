#include "pbstore/install/engine.h"

#include <algorithm>

#include "pbstore/install/catalog_session.h"
#include "pbstore/install/install_record.h"

namespace pbstore {
namespace install {

namespace {

std::vector<std::string> rule_ids(const std::vector<Finding>& findings) {
    std::vector<std::string> ids;
    for (const Finding& f : findings) ids.push_back(f.rule_id);
    return ids;
}

const CatalogEntry* find_entry(const Catalog& c, const std::string& id) {
    for (const CatalogEntry& e : c.apps)
        if (e.id == id) return &e;
    return nullptr;
}

PlanKind kind_of(Action::Type t) {
    switch (t) {
        case Action::Type::Install:
        case Action::Type::InstallLocal: return PlanKind::Install;
        case Action::Type::Update: return PlanKind::Update;
        case Action::Type::Remove: return PlanKind::Remove;
        case Action::Type::Adopt: return PlanKind::Adopt;
        case Action::Type::Replace: return PlanKind::Replace;
        case Action::Type::DeleteData: return PlanKind::DeleteData;
    }
    return PlanKind::Install;
}

void report(const ProgressFn& progress, const std::string& phase, uint64_t done, uint64_t total, const std::string& unit, const std::string& text) {
    if (!progress) return;
    Progress p;
    p.phase = phase;
    p.done = done;
    p.total = total;
    p.unit = unit;
    p.text = text;
    progress(p);
}

}  // namespace

Engine::Engine(Config config, Deps deps) : config_(std::move(config)), deps_(std::move(deps)) {}

LocateResult Engine::readers() { return locate(*deps_.mounts, deps_.open_fs); }

std::vector<std::string> Engine::subfolders(const std::string& path) {
    std::vector<std::string> names;
    std::unique_ptr<ReaderFs> fs = deps_.open_fs(path);
    if (!fs) return names;
    for (const DirEntry& e : fs->list(""))
        if (e.kind == Kind::Dir) names.push_back(e.name);
    std::sort(names.begin(), names.end());
    return names;
}

Identity Engine::identify_reader(ReaderFs& fs) { return identify(fs, deps_.identity_sources); }

Result<std::shared_ptr<ReaderHandle>> Engine::finish_open(const Candidate& candidate, std::vector<Finding> findings) {
    Result<std::shared_ptr<ReaderHandle>> r;
    r.findings = std::move(findings);
    std::unique_ptr<ReaderFs> fs = deps_.open_fs(candidate.mount_point);
    if (!fs) {
        r.findings.push_back(make_error("E-INS-03", candidate.mount_point, "the folder " + candidate.mount_point + " cannot be opened"));
        return r;
    }
    auto handle = std::make_shared<ReaderHandle>();
    handle->candidate = candidate;
    handle->fs = std::shared_ptr<ReaderFs>(fs.release());
    handle->identity = identify_reader(*handle->fs);
    if (deps_.redactor) {
        deps_.redactor->set_mount_path(candidate.mount_point);
        deps_.redactor->set_volume_label(candidate.label);
    }
    r.value = handle;
    return r;
}

Result<std::shared_ptr<ReaderHandle>> Engine::open_reader(const std::optional<std::string>& folder) {
    if (folder) {
        Opened o = open_folder(*folder, deps_.open_fs);
        if (!o.candidate.qualifies) {
            Result<std::shared_ptr<ReaderHandle>> r;
            r.findings = o.findings;
            return r;
        }
        // a drive the system lists keeps its size and file system for the display
        for (const Candidate& c : readers().candidates)
            if (c.mount_point == o.candidate.mount_point) {
                o.candidate.size_bytes = c.size_bytes;
                o.candidate.fstype = c.fstype;
            }
        return finish_open(o.candidate, {});
    }
    Pick pick = pick_single_reader(readers());
    if (!pick.reader) {
        Result<std::shared_ptr<ReaderHandle>> r;
        r.findings = pick.findings;
        return r;
    }
    return finish_open(*pick.reader, {});
}

Result<std::shared_ptr<ReaderHandle>> Engine::open_candidate(const Candidate& candidate) {
    if (!candidate.qualifies) {
        Result<std::shared_ptr<ReaderHandle>> r;
        std::string parts;
        for (const std::string& m : candidate.missing) parts += (parts.empty() ? "" : " and ") + m;
        r.findings.push_back(make_error("E-INS-03", candidate.mount_point, candidate.mount_point + " is not the top folder of a reader: " + parts + " missing"));
        return r;
    }
    return finish_open(candidate, {});
}

Result<Listing> Engine::installed(ReaderHandle& reader) {
    Result<Listing> r;
    ScanRequest req;
    req.all_recorded = true;
    ReaderSnapshot snap = scan(*reader.fs, reader.identity, req);
    Listing l;
    l.apps = derive_app_states(nullptr, snap);
    for (const Finding& f : snap.findings) r.notices.push_back(f);
    if (snap.journal_present) {
        RepairCheck rc = plan_repair(*reader.fs);
        l.interrupted = rc.plan;
        for (const Finding& f : rc.findings) r.notices.push_back(f);
    }
    r.value = std::move(l);
    return r;
}

bool Engine::ensure_catalog(std::vector<Finding>* findings, std::vector<Finding>* notices, const ProgressFn& progress) {
    report(progress, "downloading", 0, 0, "", "Downloading the catalog");
    StateStore store(*deps_.state);
    DownloadProgress dp;
    if (progress) dp = [progress](uint64_t done, uint64_t total) { report(progress, "downloading", done, total, "bytes", "Downloading the catalog"); };
    CatalogSession s = refresh_catalog(deps_.download, store, config_.catalog_url, config_.trusted_keys, dp);
    for (const Finding& f : s.notices) notices->push_back(f);
    if (!s.catalog) {
        for (const Finding& f : s.findings) findings->push_back(f);
        return false;
    }
    catalog_ = s.catalog;
    return true;
}

Result<Listing> Engine::list(ReaderHandle& reader, const ProgressFn& progress, CancelToken& cancel) {
    Result<Listing> r;
    if (cancel.cancelled()) {
        r.findings.push_back(make_error("E-INS-33", "operation", "cancelled by the user"));
        return r;
    }
    if (!ensure_catalog(&r.findings, &r.notices, progress)) {
        deps_.log->event("list", "", "", "failed", rule_ids(r.findings), r.findings.empty() ? "" : r.findings[0].message);
        return r;
    }
    ScanRequest req;
    req.all_recorded = true;
    ReaderSnapshot snap = scan(*reader.fs, reader.identity, req);
    Listing l;
    l.catalog_loaded = true;
    l.catalog_generated_at = catalog_->generated_at;
    l.apps = derive_app_states(&*catalog_, snap);
    for (const Finding& f : snap.findings) r.notices.push_back(f);
    if (snap.journal_present) {
        RepairCheck rc = plan_repair(*reader.fs);
        l.interrupted = rc.plan;
    }
    deps_.log->event("list", "", "", "ok", {});
    r.value = std::move(l);
    return r;
}

Result<PlanHandle> Engine::plan(ReaderHandle& reader, const Action& action, const ProgressFn& progress, CancelToken& cancel) {
    Result<PlanHandle> r;
    const PlanKind kind = kind_of(action.type);
    auto fail = [&](std::vector<Finding> findings) {
        r.findings = std::move(findings);
        deps_.log->event(plan_kind_name(kind), action.app_id, "", "failed", rule_ids(r.findings), r.findings.empty() ? "" : r.findings[0].message);
        return r;
    };
    if (cancel.cancelled()) return fail({make_error("E-INS-33", "operation", "cancelled by the user")});
    if (action.type == Action::Type::DeleteData) return fail({make_error("E-INS-19", action.app_id, "this kind of action is not available here")});
    const bool local = action.type == Action::Type::InstallLocal;

    // an interrupted operation has to be repaired first (spec FR-018)
    if (journal_exists(*reader.fs)) {
        r.effect = ReaderEffect::RepairNeeded;
        return fail({make_error("E-INS-23", "reader", "an earlier operation on this reader was interrupted and has to be repaired first")});
    }
    std::vector<Finding> writable = check_writable(*reader.fs);
    if (!writable.empty()) return fail(writable);

    PlanRequest req;
    req.kind = kind;
    req.app_id = action.app_id;
    req.now = deps_.clock->now();
    req.local_now = deps_.clock->local_now();
    req.installed_by = "pbstore-install " + config_.program_version;
    req.source = "catalog";
    req.source_label = "catalog";

    std::shared_ptr<PackageContent> content;
    Package package;
    std::string app_id = action.app_id;
    if (local) {
        if (!deps_.read_local_file) return fail({make_error("E-INS-27", action.local_file, "reading a package file is not available")});
        if (deps_.redactor) deps_.redactor->set_local_file(action.local_file);
        LocalPackageResult lp = read_local_package(action.local_file, deps_.read_local_file);
        if (!lp.package.ok()) return fail(lp.findings);
        package = lp.package;
        app_id = package.manifest->id;
        req.app_id = app_id;
        req.source = "local";
        req.source_label = kLocalMarking;
        req.package_sha256 = package.sha256;
        content = std::make_shared<ZipContent>(package.bytes);
        // the catalog is used only to compare; without it the install goes on offline
        std::vector<Finding> catalog_findings, catalog_notices;
        if (ensure_catalog(&catalog_findings, &catalog_notices, progress)) {
            std::vector<Finding> clashes;
            for (const CatalogEntry& e : catalog_->apps) {
                if (e.id == app_id) continue;
                if (e.menu_entry_id == package.manifest->menu_entry.id)
                    clashes.push_back(make_error("E-INS-15", e.menu_entry_id, "the menu entry " + e.menu_entry_id + " of this package belongs to the catalog app " + e.id));
                if (e.data_dir && package.manifest->data_dir && *e.data_dir == *package.manifest->data_dir)
                    clashes.push_back(make_error("E-INS-16", *e.data_dir, "the data folder " + *e.data_dir + " of this package belongs to the catalog app " + e.id));
            }
            if (!clashes.empty()) return fail(clashes);
        } else {
            r.notices.push_back(make_notice("E-INS-26", "catalog",
                                            "The catalog could not be reached, so this package is installed offline and was not compared with the apps of the catalog" +
                                                (catalog_findings.empty() ? std::string() : " (" + catalog_findings[0].message + ")")));
        }
        // an app that is installed already is updated, otherwise installed
        ScanRequest probe;
        probe.app_id = app_id;
        if (scan(*reader.fs, reader.identity, probe).installed.count(app_id)) req.kind = PlanKind::Update;
    } else if (kind != PlanKind::Remove) {
        if (!ensure_catalog(&r.findings, &r.notices, progress)) return fail(r.findings);
        const CatalogEntry* entry = find_entry(*catalog_, action.app_id);
        if (!entry) return fail({make_error("E-INS-28", action.app_id, action.app_id + " is not in the catalog")});
        if (!offered(*entry)) return fail({make_error("E-INS-28", action.app_id, "version " + entry->version + " of " + action.app_id + " has been withdrawn and is not offered")});
        report(progress, "downloading", 0, static_cast<uint64_t>(entry->package_size), "bytes", "Downloading " + entry->name);
        DownloadProgress dp;
        if (progress) dp = [progress, entry](uint64_t done, uint64_t total) {
            report(progress, "downloading", done, total ? total : static_cast<uint64_t>(entry->package_size), "bytes", "Downloading " + entry->name);
        };
        package = fetch_catalog_package(deps_.download, *entry, dp);
        if (cancel.cancelled()) return fail({make_error("E-INS-33", "operation", "cancelled by the user")});
        report(progress, "checking", 0, 0, "", "Checking the package");
        if (!package.ok()) return fail(package.findings);
        req.package_sha256 = package.sha256;
        req.catalog_generated_at = catalog_->generated_at;
        content = std::make_shared<ZipContent>(package.bytes);
    } else {
        content = std::make_shared<MapContent>();
        if (catalog_) req.catalog_generated_at = catalog_->generated_at;
    }
    req.manifest = package.manifest ? &*package.manifest : nullptr;

    ScanRequest scan_req;
    scan_req.app_id = app_id;
    scan_req.manifest = req.manifest;
    ReaderSnapshot snap = scan(*reader.fs, reader.identity, scan_req);
    PlanResult pr = make_plan(req, snap);
    for (const Finding& n : pr.notices) r.notices.push_back(n);
    if (!pr.plan) {
        bool nothing = false;
        for (const Finding& n : pr.notices) nothing = nothing || n.rule_id == "W-INS-01";
        if (pr.findings.empty() && nothing) {
            PlanHandle h;
            h.nothing_to_do = true;
            h.plan.kind = req.kind;
            h.plan.app_id = app_id;
            r.value = std::move(h);
            deps_.log->event(plan_kind_name(kind), action.app_id, "", "nothing-to-do", {"W-INS-01"});
            return r;
        }
        for (const Finding& f : r.notices) (void)f;
        return fail(pr.findings);
    }
    PlanHandle h;
    h.plan = *pr.plan;
    h.plan.delete_data = action.delete_data && kind == PlanKind::Remove;
    h.content = content;
    h.notices = pr.notices;
    h.text = render_plan(h.plan);
    r.value = std::move(h);
    return r;
}

std::string Engine::render(const Plan& plan) const { return render_plan(plan); }

ExecResult Engine::execute(ReaderHandle& reader, PlanHandle& handle, const Confirmations& given, const ProgressFn& progress, CancelToken& cancel) {
    ExecResult res;
    const std::string kind = plan_kind_name(handle.plan.kind);
    const std::string version = handle.plan.to_version.empty() ? handle.plan.from_version : handle.plan.to_version;
    if (handle.preview_only || handle.nothing_to_do || !handle.content) {
        res.findings.push_back(make_error("E-INS-25", handle.plan.app_id, "this plan cannot be carried out; make a new plan"));
        return res;
    }
    res = pbstore::install::execute(*reader.fs, handle.plan, *handle.content, given, progress, cancel);
    std::string result = res.ok() ? "ok" : res.cancelled ? "cancelled" : res.outcome == ExecResult::Outcome::RepairNeeded ? "repair-needed" : "failed";
    if (!res.missing_confirmations.empty()) result = "not-confirmed";
    deps_.log->event(kind, handle.plan.app_id, version, result, rule_ids(res.findings), res.findings.empty() ? "" : res.findings[0].message);
    return res;
}

Result<PlanHandle> Engine::plan_delete_data(ReaderHandle& reader, const Plan& removal) {
    Result<PlanHandle> r;
    std::optional<std::string> dir;
    if (removal.data) dir = removal.data->path;
    ScanRequest req;
    req.app_id = removal.app_id;
    if (dir) req.data_dir = *dir;
    ReaderSnapshot snap = scan(*reader.fs, reader.identity, req);
    DeleteDataPlanResult pr = pbstore::install::plan_delete_data(removal.app_id, dir, snap);
    r.notices = pr.notices;
    if (!pr.plan) {
        r.findings = pr.findings;
        return r;
    }
    PlanHandle h;
    h.plan = *pr.plan;
    h.content = std::make_shared<MapContent>();
    h.text = render_plan(h.plan);
    r.value = std::move(h);
    return r;
}

DeleteResult Engine::execute_delete_data(ReaderHandle& reader, PlanHandle& handle, const std::string& token1, const std::string& token2,
                                         const ProgressFn& progress, CancelToken& cancel) {
    DeleteResult res = delete_data(*reader.fs, handle.plan, token1, token2, progress, cancel);
    deps_.log->event("delete-data", handle.plan.app_id, "", res.done ? "ok" : res.cancelled ? "cancelled" : "failed", rule_ids(res.findings),
                     res.findings.empty() ? "" : res.findings[0].message);
    return res;
}

RepairCheck Engine::repair_plan(ReaderHandle& reader) { return plan_repair(*reader.fs); }

RepairResult Engine::repair(ReaderHandle& reader, const ProgressFn& progress, CancelToken& cancel) {
    (void)cancel;
    report(progress, "finishing", 0, 0, "", "Repairing the interrupted operation");
    RepairCheck rc = plan_repair(*reader.fs);
    RepairResult rr;
    if (!rc.plan) {
        rr.done = rc.findings.empty();
        rr.outcome = "nothing to repair";
        rr.findings = rc.findings;
        return rr;
    }
    rr = pbstore::install::repair(*reader.fs, *rc.plan);
    deps_.log->event("repair", rc.plan->app_id, rc.plan->to_version, rr.done ? "ok" : "failed", rule_ids(rr.findings), rr.outcome);
    return rr;
}

}  // namespace install
}  // namespace pbstore
