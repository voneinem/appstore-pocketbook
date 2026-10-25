#include "pbstore/install/desktop_config.h"

#include <algorithm>
#include <functional>
#include <set>

#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

namespace {

using json::Value;

constexpr size_t kMaxConfigBytes = 8u * 1024u * 1024u;

ParseResult fail(const char* rule, const std::string& message) {
    ParseResult r;
    r.findings.push_back(make_error(rule, "system/config/desktop/view.json", message));
    return r;
}

const Value* member(const Value& obj, const char* key) { return obj.find(key); }

// The groups are in the "view" object (observed on a real reader: {"version", "applications", "view": {"_comment", "groups"}}).
const Value* groups_in(const Value& tree) {
    const Value* view = tree.find("view");
    return view && view->is_object() ? view->find("groups") : nullptr;
}
// Keys that start with an underscore carry documentation ("_comment"), not entries.
bool is_comment_key(const std::string& key) { return !key.empty() && key[0] == '_'; }

Value* member_mut(Value& obj, const char* key) {
    for (auto& kv : obj.object)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

// ---- text helpers --------------------------------------------------------------------------------------

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// the whitespace that stands directly before `pos`
std::string ws_before(const std::string& t, size_t pos) {
    size_t b = pos;
    while (b > 0 && is_ws(t[b - 1])) --b;
    return t.substr(b, pos - b);
}

// the indentation of the line that holds `pos`
std::string line_indent(const std::string& t, size_t pos) {
    size_t line = pos;
    while (line > 0 && t[line - 1] != '\n') --line;
    size_t e = line;
    while (e < t.size() && (t[e] == ' ' || t[e] == '\t')) ++e;
    return t.substr(line, e - line);
}

std::string detect_eol(const std::string& t) { return t.find("\r\n") != std::string::npos ? "\r\n" : "\n"; }

// one indentation step: a tab, or the number of spaces of the first indented line
std::string detect_unit(const std::string& t) {
    size_t i = 0;
    while (i < t.size()) {
        const size_t nl = t.find('\n', i);
        if (nl == std::string::npos) break;
        size_t b = nl + 1, e = b;
        while (e < t.size() && (t[e] == ' ' || t[e] == '\t')) ++e;
        if (e > b && e < t.size() && t[e] != '\r' && t[e] != '\n') return t[b] == '\t' ? "\t" : std::string(e - b, ' ');
        i = nl + 1;
    }
    return "  ";
}

struct Style {
    bool pretty = false;
    std::string eol = "\n";
    std::string unit = "  ";
    std::string colon = ":", comma = ",";
};

// how a new item is laid out next to a neighbour whose leading whitespace is `lead`
Style style_from_lead(const std::string& lead, const std::string& text) {
    Style s;
    s.unit = detect_unit(text);
    const size_t nl = lead.rfind('\n');
    if (nl != std::string::npos) {
        s.pretty = true;
        s.eol = (nl > 0 && lead[nl - 1] == '\r') ? "\r\n" : "\n";
        s.colon = ": ";
    } else if (!lead.empty()) {
        s.colon = ": ";
        s.comma = ", ";
    }
    return s;
}

std::string indent_of_lead(const std::string& lead) {
    const size_t nl = lead.rfind('\n');
    return nl == std::string::npos ? std::string() : lead.substr(nl + 1);
}

std::string q(const std::string& s) { return json::quote(s); }

std::string path_object(const std::string& path, const std::string& indent, const Style& s) {
    if (!s.pretty) return "{" + q("path") + s.colon + q(path) + "}";
    return "{" + s.eol + indent + s.unit + q("path") + s.colon + q(path) + s.eol + indent + "}";
}

// {"path": ..., "title": ..., "icon": {...}, "focused_icon": {...}} laid out for a member at `indent`
std::string value_text(const EntryValue& v, const std::string& indent, const Style& s) {
    const std::string in1 = indent + s.unit;
    if (!s.pretty)
        return "{" + q("path") + s.colon + q(v.path) + s.comma + q("title") + s.colon + q(v.title) + s.comma + q("icon") + s.colon +
               path_object(v.icon_path, in1, s) + s.comma + q("focused_icon") + s.colon + path_object(v.focused_icon_path, in1, s) + "}";
    return "{" + s.eol + in1 + q("path") + s.colon + q(v.path) + "," + s.eol + in1 + q("title") + s.colon + q(v.title) + "," + s.eol + in1 +
           q("icon") + s.colon + path_object(v.icon_path, in1, s) + "," + s.eol + in1 + q("focused_icon") + s.colon +
           path_object(v.focused_icon_path, in1, s) + s.eol + indent + "}";
}

Value value_tree(const EntryValue& v) {
    Value o = Value::make_object();
    o.set("path", Value::from_string(v.path));
    o.set("title", Value::from_string(v.title));
    Value icon = Value::make_object();
    icon.set("path", Value::from_string(v.icon_path));
    o.set("icon", icon);
    Value fi = Value::make_object();
    fi.set("path", Value::from_string(v.focused_icon_path));
    o.set("focused_icon", fi);
    return o;
}

bool trees_equal(const Value& a, const Value& b) { return json::write(a) == json::write(b); }

// ---- the editor ------------------------------------------------------------------------------------------

struct Editor {
    const DesktopConfig& cfg;
    const std::string& text;
    std::vector<Splice> splices;

    explicit Editor(const DesktopConfig& c) : cfg(c), text(c.text) {}

    const Value& apps() const { return *cfg.tree.find("applications"); }
    const Value& groups() const { return *groups_in(cfg.tree); }

    const Value* group_by_title(const std::string& title) const {
        for (const Value& g : groups().array)
            if (g.get_string("title") == title) return &g;
        return nullptr;
    }

    bool compact_file() const { return text.find('\n') == std::string::npos; }

    // Inserts `build(style, indent)` as a new last item of an object or array value.
    // item_begin(i) / item_end(i) give the byte range of the i-th existing item.
    void append_item(const Value& container, bool is_object, const std::function<std::string(const Style&, const std::string&)>& build) {
        const size_t n = is_object ? container.object.size() : container.array.size();
        if (n > 0) {
            const size_t first_of_last = is_object ? container.key_begin[n - 1] : container.array[n - 1].span.begin;
            const size_t last_end = is_object ? container.object[n - 1].second.span.end : container.array[n - 1].span.end;
            const std::string lead = ws_before(text, first_of_last);
            const Style st = style_from_lead(lead, text);
            splices.push_back({last_end, last_end, "," + lead + build(st, indent_of_lead(lead))});
            return;
        }
        // an empty container: its inside is replaced
        Style st;
        st.unit = detect_unit(text);
        std::string inner;
        if (compact_file()) {
            inner = build(st, "");
        } else {
            st.pretty = true;
            st.eol = detect_eol(text);
            st.colon = ": ";
            const std::string base = line_indent(text, container.span.begin);
            const std::string mi = base + st.unit;
            inner = st.eol + mi + build(st, mi) + st.eol + base;
        }
        splices.push_back({container.span.begin + 1, container.span.end - 1, inner});
    }

    // Removes the items with the given indices of an object or array value (one splice per run).
    void remove_items(const Value& container, bool is_object, const std::set<size_t>& gone) {
        const size_t n = is_object ? container.object.size() : container.array.size();
        auto begin_of = [&](size_t i) { return is_object ? container.key_begin[i] : container.array[i].span.begin; };
        auto end_of = [&](size_t i) { return is_object ? container.object[i].second.span.end : container.array[i].span.end; };
        if (gone.empty()) return;
        if (gone.size() == n) {   // everything goes: the container becomes empty
            splices.push_back({container.span.begin + 1, container.span.end - 1, ""});
            return;
        }
        size_t i = 0;
        while (i < n) {
            if (!gone.count(i)) { ++i; continue; }
            size_t j = i;
            while (j + 1 < n && gone.count(j + 1)) ++j;
            if (j + 1 < n) splices.push_back({begin_of(i), begin_of(j + 1), ""});      // a kept item follows: take its leading space
            else splices.push_back({end_of(i - 1), end_of(j), ""});                    // at the end: the comma before goes too
            i = j + 1;
        }
    }

    std::set<size_t> ids_in_array(const Value& arr, const std::string& id) const {
        std::set<size_t> idx;
        for (size_t i = 0; i < arr.array.size(); ++i)
            if (arr.array[i].is_string() && arr.array[i].string == id) idx.insert(i);
        return idx;
    }

    const Value& apps_of(const Value& group) const { return *group.find("apps"); }

    void add_id_to_group(const Value& group, const std::string& id) {
        append_item(apps_of(group), false, [&](const Style&, const std::string&) { return q(id); });
    }

    void remove_id_from_groups(const std::string& id, const Value* except) {
        for (const Value& g : groups().array) {
            if (&g == except || !g.find("apps")) continue;
            remove_items(apps_of(g), false, ids_in_array(apps_of(g), id));
        }
    }

    EditResult finish() {
        std::sort(splices.begin(), splices.end(), [](const Splice& a, const Splice& b) { return a.begin < b.begin || (a.begin == b.begin && a.end < b.end); });
        EditResult r;
        std::string out = text;
        for (size_t i = splices.size(); i-- > 0;) out.replace(splices[i].begin, splices[i].end - splices[i].begin, splices[i].text);
        r.text = out;
        r.splices = splices;
        return r;
    }
};

EditResult refuse(const char* rule, const std::string& subject, const std::string& message) {
    EditResult r;
    r.findings.push_back(make_error(rule, subject, message));
    return r;
}

std::string group_list(const DesktopConfig& c) {
    std::string s;
    for (const std::string& t : c.group_titles()) s += (s.empty() ? "" : ", ") + std::string("\"") + t + "\"";
    return s.empty() ? "none" : s;
}

}  // namespace

EntryValue entry_value_from_manifest(const MenuEntry& menu) {
    return {std::string(kDeviceRootPrefix) + menu.path, menu.title, menu.icon, menu.focused_icon};
}

bool DesktopConfig::has_entry(const std::string& id) const {
    const Value* a = tree.find("applications");
    return a && a->find(id) != nullptr;
}

bool DesktopConfig::has_group(const std::string& title) const {
    for (const std::string& t : group_titles())
        if (t == title) return true;
    return false;
}

std::vector<std::string> DesktopConfig::group_titles() const {
    std::vector<std::string> v;
    const Value* g = groups_in(tree);
    if (g && g->is_array())
        for (const Value& e : g->array) v.push_back(e.get_string("title").value_or(""));
    return v;
}

ParseResult parse_desktop_config(const std::string& text) {
    std::string err;
    auto tree = json::parse(text, &err, kMaxConfigBytes, /*spans=*/true);
    if (!tree) return fail("E-INS-12", "the desktop configuration is not valid JSON (" + err + ")");
    if (!tree->is_object()) return fail("E-INS-13", "the desktop configuration is not an object at the top level");
    const Value* apps = member(*tree, "applications");
    if (!apps) return fail("E-INS-13", "the desktop configuration has no \"applications\" part");
    if (!apps->is_object()) return fail("E-INS-13", "\"applications\" in the desktop configuration is not an object");
    for (const auto& kv : apps->object)
        if (!is_comment_key(kv.first) && !kv.second.is_object()) return fail("E-INS-13", "\"applications\" holds an entry that is not an object (" + kv.first + ")");
    const Value* view = member(*tree, "view");
    if (!view) return fail("E-INS-13", "the desktop configuration has no \"view\" part");
    if (!view->is_object()) return fail("E-INS-13", "\"view\" in the desktop configuration is not an object");
    const Value* groups = groups_in(*tree);
    if (!groups) return fail("E-INS-13", "the desktop configuration has no \"groups\" part (in \"view\")");
    if (!groups->is_array()) return fail("E-INS-13", "\"groups\" in the desktop configuration is not a list");
    for (const Value& g : groups->array) {
        if (!g.is_object()) return fail("E-INS-13", "\"groups\" holds an element that is not an object");
        const Value* t = g.find("title");
        if (!t || !t->is_string()) return fail("E-INS-13", "a group of the desktop configuration has no \"title\" text");
        const Value* a = g.find("apps");   // optional for the reader; a group without it can not take an entry
        if (!a) continue;
        if (!a->is_array()) return fail("E-INS-13", "the \"apps\" of the group \"" + t->string + "\" is not a list");
        for (const Value& e : a->array)
            if (!e.is_string()) return fail("E-INS-13", "the \"apps\" list of the group \"" + t->string + "\" holds something that is not text");
    }
    ParseResult r;
    DesktopConfig c;
    c.text = text;
    c.sha256 = sha256_hex(text);
    c.tree = std::move(*tree);
    r.config = std::move(c);
    return r;
}

Change add_entry(const std::string& id, const EntryValue& value, const std::string& group) {
    Change c;
    c.kind = Change::Kind::Add;
    c.id = id;
    c.value = value;
    c.group = group;
    return c;
}

Change replace_entry(const std::string& id, const EntryValue& value, const std::string& group) {
    Change c = add_entry(id, value, group);
    c.kind = Change::Kind::Replace;
    return c;
}

Change remove_entry(const std::string& id) {
    Change c;
    c.kind = Change::Kind::Remove;
    c.id = id;
    return c;
}

EditResult edit(const DesktopConfig& config, const Change& change) {
    Editor ed(config);
    const std::string subject = "system/config/desktop/view.json";
    switch (change.kind) {
        case Change::Kind::Add: {
            if (config.has_entry(change.id))
                return refuse("E-INS-15", change.id, "the menu entry \"" + change.id + "\" already exists in the desktop configuration");
            const Value* group = ed.group_by_title(change.group);
            if (!group)
                return refuse("E-INS-14", subject, "the menu group \"" + change.group + "\" does not exist on the reader (groups there: " + group_list(config) + ")");
            if (!group->find("apps"))
                return refuse("E-INS-13", subject, "the menu group \"" + change.group + "\" has no \"apps\" list, so the entry can not be put in it");
            ed.append_item(ed.apps(), true, [&](const Style& st, const std::string& indent) {
                return q(change.id) + st.colon + value_text(change.value, indent, st);
            });
            ed.add_id_to_group(*group, change.id);
            return ed.finish();
        }
        case Change::Kind::Replace: {
            const Value* current = ed.apps().find(change.id);
            if (!current) return refuse("E-INS-30", change.id, "the menu entry \"" + change.id + "\" is not in the desktop configuration");
            const Value* group = ed.group_by_title(change.group);
            if (!group)
                return refuse("E-INS-14", subject, "the menu group \"" + change.group + "\" does not exist on the reader (groups there: " + group_list(config) + ")");
            if (!group->find("apps"))
                return refuse("E-INS-13", subject, "the menu group \"" + change.group + "\" has no \"apps\" list, so the entry can not be put in it");
            if (!trees_equal(*current, value_tree(change.value))) {
                size_t index = 0;
                for (size_t i = 0; i < ed.apps().object.size(); ++i)
                    if (ed.apps().object[i].first == change.id) index = i;
                const std::string lead = ws_before(config.text, ed.apps().key_begin[index]);
                const Style st = style_from_lead(lead, config.text);
                ed.splices.push_back({current->span.begin, current->span.end, value_text(change.value, indent_of_lead(lead), st)});
            }
            if (ed.ids_in_array(ed.apps_of(*group), change.id).empty()) {   // the group changed: move the id
                ed.remove_id_from_groups(change.id, nullptr);
                ed.add_id_to_group(*group, change.id);
            }
            return ed.finish();
        }
        case Change::Kind::Remove: {
            const Value& apps = ed.apps();
            std::set<size_t> gone;
            for (size_t i = 0; i < apps.object.size(); ++i)
                if (apps.object[i].first == change.id) gone.insert(i);
            ed.remove_items(apps, true, gone);
            ed.remove_id_from_groups(change.id, nullptr);
            return ed.finish();
        }
    }
    return EditResult();
}

std::string entry_value_text(const DesktopConfig& config, const std::string& id) {
    const Value* apps = config.tree.find("applications");
    const Value* entry = apps ? apps->find(id) : nullptr;
    if (!entry) return "";
    return config.text.substr(entry->span.begin, entry->span.end - entry->span.begin);
}

namespace {

// the tree an edit must produce, built on the parsed trees without looking at the text
Value expected_tree(Value tree, const Change& c) {
    Value* apps = member_mut(tree, "applications");
    Value* groups = member_mut(*member_mut(tree, "view"), "groups");
    auto apps_of = [](Value& g) { return member_mut(g, "apps"); };
    auto drop = [&](const std::string& id) {
        for (Value& g : groups->array) {
            Value* a = apps_of(g);
            if (!a) continue;
            for (size_t i = a->array.size(); i-- > 0;)
                if (a->array[i].string == id) a->array.erase(a->array.begin() + static_cast<long>(i));
        }
    };
    auto target = [&]() -> Value* {
        for (Value& g : groups->array)
            if (g.get_string("title") == c.group) return &g;
        return nullptr;
    };
    switch (c.kind) {
        case Change::Kind::Add:
            apps->object.emplace_back(c.id, value_tree(c.value));
            if (Value* t = target()) apps_of(*t)->array.push_back(Value::from_string(c.id));
            break;
        case Change::Kind::Replace: {
            if (Value* cur = member_mut(*apps, c.id.c_str())) *cur = value_tree(c.value);
            if (Value* t = target()) {
                bool listed = false;
                for (const Value& e : apps_of(*t)->array) listed = listed || e.string == c.id;
                if (!listed) {
                    drop(c.id);
                    apps_of(*t)->array.push_back(Value::from_string(c.id));
                }
            }
            break;
        }
        case Change::Kind::Remove:
            for (size_t i = 0; i < apps->object.size(); ++i)
                if (apps->object[i].first == c.id) { apps->object.erase(apps->object.begin() + static_cast<long>(i)); break; }
            drop(c.id);
            break;
    }
    return tree;
}

}  // namespace

bool verify(const std::string& old_text, const std::string& new_text, const Change& change, std::string* why) {
    auto say = [&](const std::string& s) {
        if (why) *why = s;
        return false;
    };
    ParseResult before = parse_desktop_config(old_text);
    if (!before.config) return say("the old desktop configuration is not valid: " + before.findings[0].message);
    ParseResult after = parse_desktop_config(new_text);
    if (!after.config) return say("the new desktop configuration is not valid: " + after.findings[0].message);
    if (!trees_equal(after.config->tree, expected_tree(before.config->tree, change)))
        return say("the new desktop configuration differs from the old one by more than the intended change");
    EditResult again = edit(*before.config, change);
    if (!again.text) return say("the change cannot be made on the old desktop configuration");
    if (*again.text != new_text) return say("bytes outside the changed entry differ from the old file");
    return true;
}

}  // namespace install
}  // namespace pbstore
