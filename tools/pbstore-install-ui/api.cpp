#include "api.h"

#include <openssl/rand.h>

#include <algorithm>
#include <cstdio>

#include "assets.h"
#include "pbstore/install/messages.h"
#include "pbstore/install/text_safety.h"
#include "pbstore/json.h"

namespace pbstore {
namespace install {
namespace ui {

namespace {

using json::Value;

Value make_obj() { return Value::make_object(); }
Value make_arr() { return Value::make_array(); }
Value s(const std::string& text) { return Value::from_string(neutralize(text)); }
// Text in lines (a plan, a message): line breaks stay, everything else is made safe.
Value lines(const std::string& text) { return Value::from_string(neutralize(text, true)); }
Value n(uint64_t v) { return Value::from_int(static_cast<int64_t>(v)); }
Value b(bool v) { return Value::from_bool(v); }

HttpResponse reply(int status, const Value& v) {
    HttpResponse r;
    r.status = status;
    r.body = json::write(v);
    return r;
}

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

std::string random_id() {
    unsigned char r[8];
    RAND_bytes(r, sizeof r);
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : r) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 15]);
    }
    return out;
}

// {message: {what, reader_effect, next, rule_ids}, details: [lines]}
Value message_value(const std::vector<Finding>& findings, ReaderEffect effect) {
    Value m = make_obj();
    Value ids = make_arr();
    Value details = make_arr();
    for (const Finding& f : findings) {
        ids.push(s(f.rule_id));
        details.push(s(format_finding(f)));
    }
    const Message first = findings.empty() ? Message() : describe(findings[0], effect);
    m.set("what", s(first.what));
    m.set("reader_effect", s(findings.empty() ? reader_effect_text(effect) : first.reader_effect));
    m.set("next", s(first.next));
    m.set("rule_ids", ids);
    Value out = make_obj();
    out.set("message", m);
    out.set("details", details);
    return out;
}

HttpResponse refused(int status, const std::vector<Finding>& findings, ReaderEffect effect = ReaderEffect::Unchanged) {
    return reply(status, message_value(findings, effect));
}

// A refusal that is no installer rule: a sentence, the state of the reader, the next step.
HttpResponse plain_refusal(int status, const std::string& what, const std::string& next) {
    Value m = make_obj();
    m.set("what", s(what));
    m.set("reader_effect", s(reader_effect_text(ReaderEffect::Unchanged)));
    m.set("next", s(next));
    m.set("rule_ids", make_arr());
    Value out = make_obj();
    out.set("message", m);
    out.set("details", make_arr());
    return reply(status, out);
}

HttpResponse bad_request(const std::string& why) { return plain_refusal(400, "The request was not understood: " + why, "Reload the page and try again."); }
HttpResponse no_reader() { return plain_refusal(422, "No reader is open.", "Choose the reader first."); }

const char* state_name(AppStateKind k) {
    switch (k) {
        case AppStateKind::NotInstalled: return "not-installed";
        case AppStateKind::Installed: return "installed";
        case AppStateKind::OutsideInstaller: return "outside-installer";
        case AppStateKind::UnknownApp: return "unknown";
    }
    return "";
}

const char* state_text(const AppEntry& a) {
    switch (a.state) {
        case AppStateKind::NotInstalled: return "not installed";
        case AppStateKind::Installed: return a.update_available ? "update available" : "installed";
        case AppStateKind::OutsideInstaller: return "installed outside the installer";
        case AppStateKind::UnknownApp: return "on the reader, not in the catalog";
    }
    return "";
}

Value app_value(const AppEntry& a) {
    Value v = make_obj();
    v.set("id", s(a.id));
    v.set("name", s(a.name));
    v.set("state", s(state_name(a.state)));
    v.set("state_text", s(state_text(a)));
    v.set("installed_version", s(a.installed_version));
    v.set("offered_version", s(a.offered_version));
    v.set("offered", b(a.offered));
    v.set("update_available", b(a.update_available));
    v.set("changed", b(a.changed));
    v.set("from_local", b(a.from_local));
    v.set("not_in_catalog", b(a.not_in_catalog));
    v.set("withdrawn", b(a.installed_version_withdrawn));
    v.set("menu_entry_missing", b(a.menu_entry_missing));
    v.set("source", s(a.source));
    v.set("description", s(a.description));
    Value changed = make_arr();
    for (const std::string& f : a.changed_files) changed.push(s(f));
    v.set("changed_files", changed);
    Value actions = make_arr();
    switch (a.state) {
        case AppStateKind::NotInstalled:
            if (a.offered) actions.push(s("install"));
            break;
        case AppStateKind::Installed:
            if (a.update_available) actions.push(s("update"));
            if (!a.not_in_catalog || true) actions.push(s("remove"));
            break;
        case AppStateKind::OutsideInstaller:
            if (a.offered) {
                actions.push(s("adopt"));
                actions.push(s("replace"));
            }
            break;
        case AppStateKind::UnknownApp: break;
    }
    v.set("actions", actions);
    return v;
}

Value repair_value(const RepairPlan& p) {
    Value v = make_obj();
    v.set("text", lines(p.text()));
    v.set("app_id", s(p.app_id));
    v.set("kind", s(p.kind));
    v.set("action", s(p.action == RepairPlan::Action::Undo ? "undo" : p.action == RepairPlan::Action::Finish ? "finish" : "discard"));
    return v;
}

Value listing_value(const Listing& l) {
    Value v = make_obj();
    Value apps = make_arr();
    for (const AppEntry& a : l.apps) apps.push(app_value(a));
    v.set("apps", apps);
    v.set("catalog_loaded", b(l.catalog_loaded));
    v.set("catalog_generated_at", s(l.catalog_generated_at.substr(0, 10)));
    v.set("interrupted", l.interrupted ? repair_value(*l.interrupted) : Value());
    return v;
}

Value plan_value(const Plan& p) {
    Value v = make_obj();
    v.set("kind", s(plan_kind_name(p.kind)));
    v.set("app_id", s(p.app_id));
    v.set("app_name", s(p.app_name));
    v.set("from_version", s(p.from_version));
    v.set("to_version", s(p.to_version));
    v.set("source_label", s(p.source_label));
    v.set("catalog_generated_at", s(p.catalog_generated_at.substr(0, 10)));
    Value items = make_arr();
    uint64_t bytes = 0, changing = 0;
    for (const PlanItem& i : p.items) {
        Value o = make_obj();
        o.set("op", s(i.op == ItemOp::Add ? "add" : i.op == ItemOp::Replace ? "replace" : i.op == ItemOp::Remove ? "remove" : "keep"));
        o.set("path", s(i.path));
        o.set("size", n(i.size));
        o.set("size_text", s(human(i.size)));
        o.set("sha256", s(i.sha256));
        o.set("reason", s(i.reason));
        items.push(o);
        if (i.op != ItemOp::Keep) {
            ++changing;
            if (i.op != ItemOp::Remove) bytes += i.size;
        }
    }
    v.set("items", items);
    Value summary = make_obj();
    summary.set("files_to_change", n(changing));
    summary.set("bytes_to_write", n(bytes));
    summary.set("bytes_text", s(human(bytes)));
    summary.set("space_needed_text", s(human(p.space.needed)));
    summary.set("space_free_text", s(human(p.space.free_bytes)));
    v.set("summary", summary);
    Value dirs = make_arr();
    for (const std::string& d : p.directories_to_create) dirs.push(s(d));
    v.set("directories_to_create", dirs);
    Value menu = make_obj();
    menu.set("action", s(p.menu.action == MenuChange::Action::Add ? "add" : p.menu.action == MenuChange::Action::Replace ? "change" : p.menu.action == MenuChange::Action::Remove ? "remove" : "none"));
    menu.set("id", s(p.menu.id));
    menu.set("title", s(p.menu.title));
    menu.set("group", s(p.menu.group));
    v.set("menu", menu);
    if (p.data) {
        Value d = make_obj();
        d.set("path", s(p.data->path));
        d.set("exists", b(p.data->exists));
        d.set("files", n(p.data->files));
        d.set("bytes_text", s(human(p.data->bytes)));
        v.set("data", d);
    } else {
        v.set("data", Value());
    }
    v.set("backup", s(p.backup.name));
    Value warnings = make_arr();
    for (const std::string& w : p.warnings) warnings.push(s(w));
    v.set("warnings", warnings);
    return v;
}

Value names_of(const std::vector<Confirmation>& cs) {
    Value a = make_arr();
    for (Confirmation c : cs) a.push(s(confirmation_name(c)));
    return a;
}

Value names_of(const Confirmations& cs) {
    Value a = make_arr();
    for (Confirmation c : cs) a.push(s(confirmation_name(c)));
    return a;
}

std::string url_decode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() + 0 && std::isxdigit(static_cast<unsigned char>(in[i + 1])) && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::string query_param(const std::string& query, const std::string& name) {
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t amp = query.find('&', pos);
        const std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const size_t eq = pair.find('=');
        if (pair.substr(0, eq) == name) return eq == std::string::npos ? "" : url_decode(pair.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

}  // namespace

Api::Api(Engine& engine, std::string home, std::vector<std::string> roots) : engine_(engine), home_(std::move(home)), roots_(std::move(roots)) {}

bool Api::begin_operation() {
    if (busy_.exchange(true)) return false;
    std::lock_guard<std::mutex> lock(mu_);
    progress_ = Progress();
    progress_set_ = false;
    cancel_ = new CancelToken();
    return true;
}

void Api::end_operation() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        delete cancel_;
        cancel_ = nullptr;
        progress_set_ = false;
    }
    busy_ = false;
}

ProgressFn Api::progress_callback() {
    return [this](const Progress& p) {
        std::lock_guard<std::mutex> lock(mu_);
        progress_ = p;
        progress_set_ = true;
    };
}

namespace {
// Releases the operation when the call ends, however it ends.
struct Scope {
    Api* api;
    void (Api::*end)();
    ~Scope() { (api->*end)(); }
};
}  // namespace

HttpResponse Api::handle(const HttpRequest& req) {
    const std::string& path = req.path;
    const bool post = req.method == "POST";
    if (!post && (path.empty() || path == "index.html" || path == "app.js" || path == "style.css")) return asset(path.empty() ? "index.html" : path);
    if (path == "api/state") return post ? plain_refusal(405, "This address is read with GET.", "Reload the page.") : state();
    if (path == "api/readers") return post ? plain_refusal(405, "This address is read with GET.", "Reload the page.") : readers();
    if (path == "api/folders") return post ? plain_refusal(405, "This address is read with GET.", "Reload the page.") : folders(req);
    if (path == "api/installed") return post ? plain_refusal(405, "This address is read with GET.", "Reload the page.") : installed();
    const char* post_only[] = {"api/reader/open", "api/list", "api/plan", "api/preview", "api/execute", "api/cancel", "api/repair/plan", "api/repair"};
    for (const char* p : post_only) {
        if (path != p) continue;
        if (!post) return plain_refusal(405, "This address only accepts POST.", "Use the page.");
        if (path == "api/reader/open") return open_reader(req);
        if (path == "api/list") return list();
        if (path == "api/plan") return plan(req, false);
        if (path == "api/preview") return plan(req, true);
        if (path == "api/execute") return execute(req);
        if (path == "api/cancel") return cancel();
        if (path == "api/repair/plan") return repair_plan();
        return repair(req);
    }
    return plain_refusal(404, "There is nothing at this address.", "Reload the page.");
}

HttpResponse Api::asset(const std::string& name) {
    const Asset* a = find_asset(name);
    if (!a) return plain_refusal(404, "There is no such page.", "Reload the page.");
    HttpResponse r;
    r.content_type = a->content_type;
    r.body = a->content;
    return r;
}

HttpResponse Api::state() {
    Value v = make_obj();
    v.set("busy", b(busy_.load()));
    std::lock_guard<std::mutex> lock(mu_);
    if (progress_set_) {
        Value o = make_obj();
        o.set("phase", s(progress_.phase));
        o.set("done", n(progress_.done));
        o.set("total", n(progress_.total));
        o.set("unit", s(progress_.unit));
        o.set("text", s(progress_.text));
        v.set("operation", o);
    } else {
        v.set("operation", Value());
    }
    v.set("reader_open", b(reader_ != nullptr));
    return reply(200, v);
}

HttpResponse Api::readers() {
    LocateResult lr = engine_.readers();
    Value v = make_obj();
    Value cands = make_arr();
    size_t qualifying = 0;
    for (const Candidate& c : lr.candidates) {
        Value o = make_obj();
        o.set("label", s(c.label));
        o.set("mount_point", s(c.mount_point));
        o.set("size_bytes", n(c.size_bytes));
        o.set("size_text", s(c.size_bytes ? human(c.size_bytes) : std::string("size unknown")));
        o.set("qualifies", b(c.qualifies));
        Value missing = make_arr();
        for (const std::string& m : c.missing) missing.push(s(m));
        o.set("missing", missing);
        cands.push(o);
        if (c.qualifies) ++qualifying;
    }
    v.set("candidates", cands);
    Value notes = make_arr();
    for (const std::string& note : lr.notes) notes.push(s(note));
    v.set("notes", notes);
    v.set("several", b(qualifying > 1));
    Pick pick = pick_single_reader(lr);
    if (pick.reader) {
        v.set("message", Value());
    } else {
        const Value mv = message_value(pick.findings, ReaderEffect::Unchanged);
        v.set("message", *mv.find("message"));
    }
    return reply(200, v);
}

HttpResponse Api::open_reader(const HttpRequest& req) {
    std::optional<std::string> folder;
    if (!req.body.empty()) {
        auto parsed = json::parse(req.body, nullptr, 1 << 20);
        if (!parsed || !parsed->is_object()) return bad_request("the body is not a JSON object");
        auto f = parsed->get_string("folder");
        if (f && !f->empty()) folder = *f;
    }
    if (busy_) return plain_refusal(409, "Another step is still running.", "Wait for it to finish, or cancel it.");
    auto r = engine_.open_reader(folder);
    if (!r.ok()) return refused(422, r.findings);
    std::lock_guard<std::mutex> lock(mu_);
    reader_ = *r.value;
    plans_.clear();
    Value v = make_obj();
    Value reader = make_obj();
    reader.set("label", s(reader_->candidate.label));
    reader.set("mount_point", s(reader_->candidate.mount_point));
    reader.set("size_text", s(reader_->candidate.size_bytes ? human(reader_->candidate.size_bytes) : std::string("size unknown")));
    v.set("reader", reader);
    return reply(200, v);
}

HttpResponse Api::folders(const HttpRequest& req) {
    const std::string path = query_param(req.query, "path");
    Value v = make_obj();
    Value list = make_arr();
    auto add = [&](const std::string& name, const std::string& full) {
        Value o = make_obj();
        o.set("name", s(name));
        o.set("path", s(full));
        list.push(o);
    };
    if (path.empty()) {
        v.set("path", s(""));
        v.set("parent", s(""));
        add("Home folder", home_);
        for (const std::string& r : roots_) add(r, r);
        for (const Candidate& c : engine_.readers().candidates) add(c.label + " (" + c.mount_point + ")", c.mount_point);
        v.set("folders", list);
        return reply(200, v);
    }
    // only places the user can reasonably choose from: below the home folder, the removable-media folders and the drives found
    bool allowed = path[0] == '/' && path.find("..") == std::string::npos && path.find('\0') == std::string::npos;
    std::vector<std::string> bases = roots_;
    bases.push_back(home_);
    bases.push_back("/run/media");
    bases.push_back("/media");
    bases.push_back("/mnt");
    for (const Candidate& c : engine_.readers().candidates) bases.push_back(c.mount_point);
    bool below = false;
    for (const std::string& base : bases)
        if (!base.empty() && (path == base || path.compare(0, base.size() + 1, base + "/") == 0)) below = true;
    if (!allowed || !below) return plain_refusal(422, "This folder cannot be shown.", "Choose a folder below your home folder or the reader's drive.");
    std::string clean = path;
    while (clean.size() > 1 && clean.back() == '/') clean.pop_back();
    v.set("path", s(clean));
    const size_t slash = clean.rfind('/');
    v.set("parent", s(slash == 0 || slash == std::string::npos ? "" : clean.substr(0, slash)));
    for (const std::string& name : engine_.subfolders(clean)) add(name, clean + "/" + name);
    v.set("folders", list);
    return reply(200, v);
}

HttpResponse Api::installed() {
    std::shared_ptr<ReaderHandle> reader;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reader = reader_;
    }
    if (!reader) return no_reader();
    auto r = engine_.installed(*reader);
    if (!r.ok()) return refused(422, r.findings);
    Value v = listing_value(*r.value);
    Value notices = make_arr();
    for (const Finding& f : r.notices) notices.push(s(format_finding(f)));
    v.set("notices", notices);
    return reply(200, v);
}

HttpResponse Api::list() {
    std::shared_ptr<ReaderHandle> reader;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reader = reader_;
    }
    if (!reader) return no_reader();
    if (!begin_operation()) return plain_refusal(409, "Another step is still running.", "Wait for it to finish, or cancel it.");
    Scope scope{this, &Api::end_operation};
    auto r = engine_.list(*reader, progress_callback(), *cancel_);
    if (!r.ok()) return refused(422, r.findings);
    Value v = listing_value(*r.value);
    Value notices = make_arr();
    for (const Finding& f : r.notices) notices.push(s(format_finding(f)));
    v.set("notices", notices);
    return reply(200, v);
}

HttpResponse Api::plan(const HttpRequest& req, bool preview) {
    auto parsed = json::parse(req.body.empty() ? "{}" : req.body, nullptr, 1 << 20);
    if (!parsed || !parsed->is_object()) return bad_request("the body is not a JSON object");
    const std::string action_name = parsed->get_string("action").value_or("");
    Action action;
    if (action_name == "install") action.type = Action::Type::Install;
    else if (action_name == "update") action.type = Action::Type::Update;
    else if (action_name == "remove") action.type = Action::Type::Remove;
    else if (action_name == "adopt") action.type = Action::Type::Adopt;
    else if (action_name == "replace") action.type = Action::Type::Replace;
    else if (action_name == "install-local") action.type = Action::Type::InstallLocal;
    else return bad_request("unknown action \"" + action_name + "\"");
    action.app_id = parsed->get_string("app_id").value_or("");
    action.local_file = parsed->get_string("local_file").value_or("");
    action.delete_data = parsed->get_bool("delete_data").value_or(false);
    if (action.type == Action::Type::InstallLocal ? action.local_file.empty() : action.app_id.empty()) return bad_request("the app or the file is missing");

    std::shared_ptr<ReaderHandle> reader;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reader = reader_;
    }
    if (!reader) return no_reader();
    if (!begin_operation()) return plain_refusal(409, "Another step is still running.", "Wait for it to finish, or cancel it.");
    Scope scope{this, &Api::end_operation};
    auto r = engine_.plan(*reader, action, progress_callback(), *cancel_);
    if (!r.ok()) return refused(422, r.findings, r.effect);
    Value v = make_obj();
    Value notices = make_arr();
    for (const Finding& f : r.notices) notices.push(s(format_finding(f)));
    v.set("notices", notices);
    if (r.value->nothing_to_do) {
        v.set("nothing_to_do", b(true));
        v.set("plan_id", s(""));
        v.set("text", lines("Nothing to do: " + action.app_id + " is already as offered."));
        v.set("confirmations", make_arr());
        v.set("dry_run_only", b(preview));
        return reply(200, v);
    }
    auto handle = std::make_shared<PlanHandle>(std::move(*r.value));
    handle->preview_only = preview;
    const std::string id = random_id();
    {
        std::lock_guard<std::mutex> lock(mu_);
        plans_[id] = handle;
    }
    v.set("nothing_to_do", b(false));
    v.set("plan_id", s(id));
    v.set("text", lines(handle->text));
    v.set("structured", plan_value(handle->plan));
    v.set("confirmations", names_of(handle->plan.confirmations));
    v.set("dry_run_only", b(preview));
    return reply(200, v);
}

HttpResponse Api::execute(const HttpRequest& req) {
    auto parsed = json::parse(req.body.empty() ? "{}" : req.body, nullptr, 1 << 20);
    if (!parsed || !parsed->is_object()) return bad_request("the body is not a JSON object");
    const std::string id = parsed->get_string("plan_id").value_or("");
    Confirmations given;
    if (const Value* cs = parsed->find("confirmations")) {
        if (!cs->is_array()) return bad_request("confirmations must be a list");
        for (const Value& c : cs->array) {
            Confirmation k;
            if (!c.is_string() || !confirmation_from_name(c.string, &k)) return plain_refusal(422, "A confirmation that is not known was sent.", "Reload the page and confirm again.");
            given.insert(k);
        }
    }
    std::string token_folder, token_count;
    if (const Value* t = parsed->find("tokens")) {
        token_folder = t->get_string("folder").value_or("");
        token_count = t->get_string("count").value_or("");
    }
    std::shared_ptr<ReaderHandle> reader;
    std::shared_ptr<PlanHandle> handle;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reader = reader_;
        auto it = plans_.find(id);
        if (it != plans_.end()) handle = it->second;
    }
    if (!reader) return no_reader();
    if (!handle) return plain_refusal(422, "This plan is not known to this session.", "Make a new plan.");
    if (handle->preview_only) return plain_refusal(422, "This plan was made as a preview and cannot be carried out.", "Make a new plan without the preview.");
    if (!begin_operation()) return plain_refusal(409, "Another step is still running.", "Wait for it to finish, or cancel it.");
    Scope scope{this, &Api::end_operation};

    if (handle->plan.kind == PlanKind::DeleteData) {
        std::vector<Confirmation> missing;
        for (Confirmation c : handle->plan.confirmations)
            if (!given.count(c)) missing.push_back(c);
        if (!missing.empty()) {
            Value v = message_value({make_error("E-INS-19", handle->plan.data ? handle->plan.data->path : "data folder", "the data folder is deleted only after both confirmations")}, ReaderEffect::Unchanged);
            v.set("missing", names_of(missing));
            return reply(422, v);
        }
        DeleteResult d = engine_.execute_delete_data(*reader, *handle, token_folder, token_count, progress_callback(), *cancel_);
        if (d.wrong_token_1 || d.wrong_token_2) {
            HttpResponse r = plain_refusal(422, std::string("The confirmation does not match: ") + (d.wrong_token_1 ? "the folder name " : "") + (d.wrong_token_2 ? "the number of files " : "") + "was not typed as shown. The data folder was kept.",
                                           "Type exactly what is shown, or keep the data folder.");
            return r;
        }
        if (!d.done) return refused(422, d.findings);
        {
            std::lock_guard<std::mutex> lock(mu_);
            plans_.erase(id);
        }
        Value v = make_obj();
        v.set("done", b(true));
        v.set("text", s("The data folder " + handle->plan.data->path + " was deleted (" + std::to_string(d.files_deleted) + " files)."));
        return reply(200, v);
    }

    ExecResult res = engine_.execute(*reader, *handle, given, progress_callback(), *cancel_);
    if (!res.missing_confirmations.empty()) {
        Value v = message_value({make_error("E-INS-25", handle->plan.app_id, "the plan needs further confirmations before anything is written")}, ReaderEffect::Unchanged);
        v.set("missing", names_of(res.missing_confirmations));
        return reply(422, v);
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        plans_.erase(id);
    }
    if (!res.ok()) {
        Value v = message_value(res.findings, res.effect);
        v.set("outcome", s(res.outcome == ExecResult::Outcome::Undone ? "undone" : res.outcome == ExecResult::Outcome::RepairNeeded ? "repair-needed" : "not-started"));
        v.set("cancelled", b(res.cancelled));
        return reply(422, v);
    }
    Value v = make_obj();
    v.set("done", b(true));
    v.set("text", lines(completion_text(handle->plan)));
    v.set("cancel_too_late", b(res.cancel_too_late));
    if (handle->plan.kind == PlanKind::Remove && handle->plan.delete_data) {
        auto dd = engine_.plan_delete_data(*reader, handle->plan);
        if (dd.ok()) {
            auto dh = std::make_shared<PlanHandle>(std::move(*dd.value));
            const std::string did = random_id();
            {
                std::lock_guard<std::mutex> lock(mu_);
                plans_[did] = dh;
            }
            Value o = make_obj();
            o.set("plan_id", s(did));
            o.set("folder", s(dh->plan.data->path));
            o.set("files", n(dh->plan.data->files));
            o.set("size_text", s(human(dh->plan.data->bytes)));
            o.set("question_1", s(delete_data_question_1(dh->plan)));
            o.set("question_2", s(delete_data_question_2(dh->plan)));
            o.set("text", lines(dh->text));
            v.set("data_deletion", o);
        }
    }
    return reply(200, v);
}

HttpResponse Api::cancel() {
    std::lock_guard<std::mutex> lock(mu_);
    if (cancel_) cancel_->cancel();
    Value v = make_obj();
    v.set("ok", b(true));
    v.set("busy", b(busy_.load()));
    return reply(200, v);
}

HttpResponse Api::repair_plan() {
    std::shared_ptr<ReaderHandle> reader;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reader = reader_;
    }
    if (!reader) return no_reader();
    RepairCheck rc = engine_.repair_plan(*reader);
    Value v = make_obj();
    v.set("interrupted", b(rc.plan.has_value()));
    if (rc.plan) {
        Value r = repair_value(*rc.plan);
        v.set("text", *r.find("text"));
        v.set("action", *r.find("action"));
        v.set("app_id", *r.find("app_id"));
        v.set("confirmations", names_of(Confirmations{Confirmation::Repair}));
    } else if (!rc.findings.empty()) {
        return refused(422, rc.findings);
    }
    return reply(200, v);
}

HttpResponse Api::repair(const HttpRequest& req) {
    auto parsed = json::parse(req.body.empty() ? "{}" : req.body, nullptr, 1 << 20);
    if (!parsed || !parsed->is_object()) return bad_request("the body is not a JSON object");
    bool confirmed = false;
    if (const Value* cs = parsed->find("confirmations"))
        for (const Value& c : cs->array) confirmed = confirmed || (c.is_string() && c.string == "repair");
    std::shared_ptr<ReaderHandle> reader;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reader = reader_;
    }
    if (!reader) return no_reader();
    if (!confirmed) {
        Value v = message_value({make_error("E-INS-23", "reader", "an interrupted operation is repaired only after you confirm the repair")}, ReaderEffect::RepairNeeded);
        v.set("missing", names_of(Confirmations{Confirmation::Repair}));
        return reply(422, v);
    }
    if (!begin_operation()) return plain_refusal(409, "Another step is still running.", "Wait for it to finish, or cancel it.");
    Scope scope{this, &Api::end_operation};
    RepairResult rr = engine_.repair(*reader, progress_callback(), *cancel_);
    if (!rr.done) return refused(422, rr.findings, ReaderEffect::RepairNeeded);
    Value v = make_obj();
    v.set("done", b(true));
    v.set("outcome", s(rr.outcome));
    v.set("text", s("The interrupted operation was " + rr.outcome + ". The reader is in one complete state again."));
    return reply(200, v);
}

}  // namespace ui
}  // namespace install
}  // namespace pbstore
