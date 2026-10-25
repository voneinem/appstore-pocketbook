#include "pbstore/install/journal.h"

#include "pbstore/install/desktop_config.h"
#include "pbstore/install/install_record.h"
#include "pbstore/json.h"
#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

namespace {

using json::Value;

constexpr const char* kViewPath = "system/config/desktop/view.json";

Value opt_string(const std::string& s) { return s.empty() ? Value() : Value::from_string(s); }

std::string get_or_empty(const Value& v, const char* key) {
    const Value* f = v.find(key);
    return f && f->is_string() ? f->string : std::string();
}

bool string_array(const Value* v, std::vector<std::string>* out) {
    if (!v) return true;
    if (!v->is_array()) return false;
    for (const Value& e : v->array) {
        if (!e.is_string()) return false;
        out->push_back(e.string);
    }
    return true;
}

}  // namespace

Intent intent_from_plan(const Plan& plan) {
    Intent in;
    in.kind = plan_kind_name(plan.kind);
    in.app_id = plan.app_id;
    in.from_version = plan.from_version;
    in.to_version = plan.to_version;
    for (const PlanItem& it : plan.items) {
        if (it.op == ItemOp::Keep) continue;
        IntentItem x;
        x.path = it.path;
        if (it.op == ItemOp::Add || it.op == ItemOp::Replace) {
            x.new_sha256 = it.sha256;
            x.new_size = it.size;
        }
        if (it.op == ItemOp::Replace || it.op == ItemOp::Remove) {
            x.old_sha256 = it.old_sha256;
            x.vacate = true;
        }
        in.items.push_back(x);
    }
    in.directories_to_create = plan.directories_to_create;
    in.directories_to_remove = plan.directories_to_remove;
    in.launcher = plan.launcher;
    if (!plan.view_new_text.empty() || plan.menu.action == MenuChange::Action::Remove) {
        in.view_old_sha256 = plan.view_old_sha256;
        in.view_new_sha256 = plan.view_new_sha256;
        in.backup_name = plan.backup.name;
    }
    in.record_old_sha256 = plan.record_old_sha256;
    in.record_new_sha256 = plan.record_new_sha256;
    in.delete_after_commit = plan.backup.delete_after_commit;
    return in;
}

std::string with_checksum_line(const std::string& body) { return body + "sha256:" + sha256_hex(body) + "\n"; }

bool strip_checksum_line(const std::string& text, std::string* body) {
    // the last line is "sha256:" + 64 hex characters + "\n"
    constexpr size_t kLine = 7 + 64 + 1;
    if (text.size() < kLine) return false;
    const size_t at = text.size() - kLine;
    if (at != 0 && text[at - 1] != '\n') return false;
    if (text.compare(at, 7, "sha256:") != 0 || text.back() != '\n') return false;
    const std::string hex = text.substr(at + 7, 64);
    const std::string b = text.substr(0, at);
    if (hex != sha256_hex(b)) return false;
    if (body) *body = b;
    return true;
}

std::string intent_text(const Intent& in) {
    Value v = Value::make_object();
    v.set("journal_format", Value::from_int(in.journal_format));
    v.set("kind", Value::from_string(in.kind));
    v.set("app_id", Value::from_string(in.app_id));
    v.set("from_version", opt_string(in.from_version));
    v.set("to_version", opt_string(in.to_version));
    Value items = Value::make_array();
    for (const IntentItem& it : in.items) {
        Value o = Value::make_object();
        o.set("path", Value::from_string(it.path));
        o.set("new_sha256", opt_string(it.new_sha256));
        o.set("new_size", Value::from_int(static_cast<int64_t>(it.new_size)));
        o.set("old_sha256", opt_string(it.old_sha256));
        o.set("vacate", Value::from_bool(it.vacate));
        items.push(o);
    }
    v.set("items", items);
    Value dirs = Value::make_array();
    for (const std::string& d : in.directories_to_create) dirs.push(Value::from_string(d));
    v.set("directories_to_create", dirs);
    Value rm = Value::make_array();
    for (const std::string& d : in.directories_to_remove) rm.push(Value::from_string(d));
    v.set("directories_to_remove", rm);
    v.set("launcher", opt_string(in.launcher));
    if (in.view_new_sha256.empty() && in.view_old_sha256.empty()) {
        v.set("view", Value());
    } else {
        Value o = Value::make_object();
        o.set("old_sha256", opt_string(in.view_old_sha256));
        o.set("new_sha256", opt_string(in.view_new_sha256));
        v.set("view", o);
    }
    Value rec = Value::make_object();
    rec.set("old_sha256", opt_string(in.record_old_sha256));
    rec.set("new_sha256", opt_string(in.record_new_sha256));
    v.set("record", rec);
    if (in.backup_name.empty() && in.delete_after_commit.empty()) {
        v.set("backup", Value());
    } else {
        Value o = Value::make_object();
        o.set("name", opt_string(in.backup_name));
        Value del = Value::make_array();
        for (const std::string& d : in.delete_after_commit) del.push(Value::from_string(d));
        o.set("delete_after_commit", del);
        v.set("backup", o);
    }
    return with_checksum_line(json::write(v));
}

IntentParse parse_intent(const std::string& text) {
    IntentParse r;
    std::string body;
    if (!strip_checksum_line(text, &body)) return r;
    auto parsed = json::parse(body, nullptr, 4u << 20);
    if (!parsed || !parsed->is_object()) return r;
    const Value& v = *parsed;
    const auto format = v.get_int("journal_format");
    if (!format) return r;
    if (*format > kJournalFormat) {
        r.newer_format = true;
        return r;
    }
    if (*format != kJournalFormat) return r;
    Intent in;
    in.journal_format = static_cast<int>(*format);
    in.kind = get_or_empty(v, "kind");
    in.app_id = get_or_empty(v, "app_id");
    in.from_version = get_or_empty(v, "from_version");
    in.to_version = get_or_empty(v, "to_version");
    if (in.kind.empty() || in.app_id.empty()) return r;
    const Value* items = v.find("items");
    if (!items || !items->is_array()) return r;
    for (const Value& o : items->array) {
        if (!o.is_object()) return r;
        IntentItem it;
        it.path = get_or_empty(o, "path");
        it.new_sha256 = get_or_empty(o, "new_sha256");
        it.new_size = static_cast<uint64_t>(o.get_int("new_size").value_or(0));
        it.old_sha256 = get_or_empty(o, "old_sha256");
        it.vacate = o.get_bool("vacate").value_or(false);
        if (it.path.empty() || !valid_relative_path(it.path)) return r;
        in.items.push_back(it);
    }
    if (!string_array(v.find("directories_to_create"), &in.directories_to_create)) return r;
    if (!string_array(v.find("directories_to_remove"), &in.directories_to_remove)) return r;
    for (const std::string& d : in.directories_to_create) if (!valid_relative_path(d)) return r;
    for (const std::string& d : in.directories_to_remove) if (!valid_relative_path(d)) return r;
    in.launcher = get_or_empty(v, "launcher");
    if (const Value* view = v.find("view")) {
        if (view->is_object()) {
            in.view_old_sha256 = get_or_empty(*view, "old_sha256");
            in.view_new_sha256 = get_or_empty(*view, "new_sha256");
        }
    }
    if (const Value* rec = v.find("record")) {
        if (rec->is_object()) {
            in.record_old_sha256 = get_or_empty(*rec, "old_sha256");
            in.record_new_sha256 = get_or_empty(*rec, "new_sha256");
        }
    }
    if (const Value* bk = v.find("backup")) {
        if (bk->is_object()) {
            in.backup_name = get_or_empty(*bk, "name");
            if (!string_array(bk->find("delete_after_commit"), &in.delete_after_commit)) return r;
        }
    }
    r.intent = std::move(in);
    r.sha256 = sha256_hex(text);
    return r;
}

std::string marker_text(const std::string& name, const std::string& intent_sha256) { return with_checksum_line(name + " " + intent_sha256 + "\n"); }

bool marker_valid(const std::string& text, const std::string& name, const std::string& intent_sha256) {
    std::string body;
    return strip_checksum_line(text, &body) && body == name + " " + intent_sha256 + "\n";
}

bool write_journal_file(ReaderFs& fs, const Path& dir, const std::string& name, const std::string& content) {
    const Path tmp = join_path(dir, name + ".tmp");
    const Path final_path = join_path(dir, name);
    if (fs.stat(tmp).exists() && !fs.remove_file(tmp)) return false;   // a leftover of an earlier attempt of ours
    if (!fs.create_exclusive(tmp, content, true)) return false;
    if (!fs.rename_free(tmp, final_path)) {
        fs.remove_file(tmp);
        return false;
    }
    return fs.flush_dir(dir);
}

std::string journal_path(const std::string& relative) { return std::string(kJournalDir) + "/" + relative; }
std::string staged_path(const std::string& path) { return journal_path("new/" + path); }
std::string vacated_path(const std::string& path) { return journal_path("old/" + path); }

bool journal_exists(ReaderFs& fs) { return fs.stat(kJournalDir).exists(); }

bool delete_tree(ReaderFs& fs, const Path& dir) {
    const Stat st = fs.stat(dir);
    if (!st.exists()) return true;
    if (st.kind != Kind::Dir) return fs.remove_file(dir);
    bool ok = true;
    for (const DirEntry& e : fs.list(dir)) {
        const Path p = join_path(dir, e.name);
        if (e.kind == Kind::Dir) ok = delete_tree(fs, p) && ok;
        else ok = fs.remove_file(p) && ok;
    }
    return fs.remove_dir(dir) && ok;
}

bool delete_journal(ReaderFs& fs) {
    if (!journal_exists(fs)) return true;
    bool ok = true;
    // 1. the staged and the vacated files (the journal keeps its meaning while they go)
    ok = delete_tree(fs, journal_path("old")) && ok;
    ok = delete_tree(fs, journal_path("new")) && ok;
    // 2. the markers and the intent, in the order that keeps every state classifiable
    for (const char* name : {kPreparedName, kIntentName, kCommitName}) {
        const Path p = journal_path(name);
        if (fs.stat(p).exists()) ok = fs.remove_file(p) && ok;
    }
    // 3. whatever else is in the folder (temporary files), then the folder
    for (const DirEntry& e : fs.list(kJournalDir)) {
        const Path p = journal_path(e.name);
        if (e.kind == Kind::Dir) ok = delete_tree(fs, p) && ok;
        else ok = fs.remove_file(p) && ok;
    }
    return fs.remove_dir(kJournalDir) && ok;
}

std::vector<std::string> verify_live_state(ReaderFs& fs, const Intent& intent) {
    std::vector<std::string> problems;
    auto hash_of = [&](const std::string& path, std::string* hex) {
        std::string data;
        if (!fs.read_file(path, 512ull * 1024 * 1024, &data)) return false;
        *hex = sha256_hex(data);
        return true;
    };
    for (const IntentItem& it : intent.items) {
        const Stat st = fs.stat(it.path);
        if (!it.new_sha256.empty()) {
            std::string hex;
            if (st.kind != Kind::File) problems.push_back(it.path + " is missing");
            else if (!hash_of(it.path, &hex)) problems.push_back(it.path + " cannot be read");
            else if (hex != it.new_sha256) problems.push_back(it.path + " does not have the expected content");
        } else if (st.exists()) {
            problems.push_back(it.path + " should be gone but is still there");
        }
    }
    if (!intent.view_new_sha256.empty()) {
        std::string text;
        if (!fs.read_file(kViewPath, 64u << 20, &text)) problems.push_back(std::string(kViewPath) + " cannot be read");
        else if (sha256_hex(text) != intent.view_new_sha256) problems.push_back(std::string(kViewPath) + " does not have the expected content");
        else if (!parse_desktop_config(text).config) problems.push_back(std::string(kViewPath) + " is not a valid desktop configuration");
    }
    const std::string rec = record_path(intent.app_id);
    if (!intent.record_new_sha256.empty()) {
        std::string hex;
        if (fs.stat(rec).kind != Kind::File) problems.push_back(rec + " is missing");
        else if (!hash_of(rec, &hex) || hex != intent.record_new_sha256) problems.push_back(rec + " does not have the expected content");
    } else if (!intent.record_old_sha256.empty() && fs.stat(rec).exists()) {
        problems.push_back(rec + " should be gone but is still there");
    }
    return problems;
}

}  // namespace install
}  // namespace pbstore
