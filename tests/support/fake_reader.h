#pragma once
// Test helper: a directory tree shaped like the observed InkPad One storage, with anonymized ids. It never
// holds real data. It can be written to a directory (for the command-line tests) or loaded into the in-memory
// volume CrashFs (for the executor and fault-matrix tests).

#include <sys/stat.h>

#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "crash_fs.h"
#include "pbstore/json.h"
#include "sample_app.h"

namespace pbtest {

class FakeReader {
public:
    enum Style { Pretty2, Pretty4, Tabs, Minified };

    struct Entry {
        std::string id, title, path, icon, focused_icon;   // path as the device sees it: /mnt/ext1/...
    };
    struct Group {
        std::string title;
        std::vector<std::string> apps;
    };

    explicit FakeReader(Style style = Pretty2) : style_(style) {
        for (const char* d : {"applications", "applications/icons", "system", "system/config", "system/config/desktop"}) dirs.insert(d);
        entries = {
            {"Dictionary", "Dictionary", "/ebrmain/vendor/dictionary.app", "icons/dictionary.bmp", "icons/dictionary_f.bmp"},
            {"U_AlphaApp", "Alpha", "/mnt/ext1/applications/alpha.app", "applications/icons/alpha.bmp", "applications/icons/alpha_f.bmp"},
            {"U_BetaApp", "Beta", "/mnt/ext1/applications/beta.app", "applications/icons/beta.bmp", "applications/icons/beta_f.bmp"},
            {"U_GammaApp", "Gamma", "/mnt/ext1/applications/gamma.app", "applications/icons/gamma.bmp", "applications/icons/gamma_f.bmp"},
        };
        groups = {{"@General", {"Dictionary", "U_AlphaApp", "U_BetaApp"}}, {"@Favorites", {"U_GammaApp"}}};
        for (const char* app : {"alpha", "beta", "gamma"}) {
            files[std::string("applications/") + app + ".app"] = std::string("#!/bin/sh\necho ") + app + "\n";
            files[std::string("applications/icons/") + app + ".bmp"] = pseudo_bytes(64, static_cast<unsigned>(app[0]));
            files[std::string("applications/icons/") + app + "_f.bmp"] = pseudo_bytes(64, static_cast<unsigned>(app[0]) + 1);
        }
        files["system/config/other.cfg"] = "setting=1\n";
        sync_view();
    }

    std::map<std::string, std::string> files;   // relative path -> content
    std::set<std::string> dirs;
    std::vector<Entry> entries;
    std::vector<Group> groups;

    static constexpr const char* kViewPath = "system/config/desktop/view.json";

    // the desktop configuration text for the current entries and groups
    std::string view_json() const {
        using pbstore::json::Value;
        Value top = Value::make_object();
        top.set("version", Value::from_int(3));
        Value layout = Value::make_object();
        layout.set("columns", Value::from_int(4));
        layout.set("rows", Value::from_int(5));
        top.set("layout", layout);
        Value apps = Value::make_object();
        {   // as on a real reader: documentation under a key that starts with an underscore
            Value note = Value::make_array();
            note.push(Value::from_string("Fields - application key id, must start from \"U_\" for user application\n"));
            note.push(Value::from_string("end."));
            apps.set("_comment", note);
        }
        for (const Entry& e : entries) {
            Value v = Value::make_object();
            v.set("path", Value::from_string(e.path));
            v.set("title", Value::from_string(e.title));
            Value icon = Value::make_object();
            icon.set("path", Value::from_string(e.icon));
            v.set("icon", icon);
            Value fi = Value::make_object();
            fi.set("path", Value::from_string(e.focused_icon));
            v.set("focused_icon", fi);
            apps.set(e.id, v);
        }
        top.set("applications", apps);
        Value gs = Value::make_array();
        for (const Group& g : groups) {
            Value o = Value::make_object();
            o.set("title", Value::from_string(g.title));
            Value list = Value::make_array();
            for (const std::string& id : g.apps) list.push(Value::from_string(id));
            o.set("apps", list);
            gs.push(o);
        }
        Value view = Value::make_object();   // the groups are inside "view", as on a real reader
        Value vnote = Value::make_array();
        vnote.push(Value::from_string("\"groups\" - array of application groups\n"));
        vnote.push(Value::from_string("end."));
        view.set("_comment", vnote);
        view.set("groups", gs);
        top.set("view", view);
        Value flags = Value::make_array();
        flags.push(Value::from_bool(true));
        flags.push(Value());
        top.set("flags", flags);
        std::string out;
        emit(top, 0, &out);
        return style_ == Minified ? out : out + "\n";
    }

    void sync_view() { files[kViewPath] = view_json(); }

    // An app copied by hand from a release ZIP: its files, an entry in the desktop configuration, no record.
    void add_hand_installed(const SampleApp& app) {
        for (const ZEntry& f : app.files) put_file(f.name, f.data);
        const pbstore::json::Value* menu = app.manifest.find("menu_entry");
        Entry e;
        e.id = menu->get_string("id").value();
        e.title = menu->get_string("title").value();
        e.path = "/mnt/ext1/" + menu->get_string("path").value();
        e.icon = menu->get_string("icon").value();
        e.focused_icon = menu->get_string("focused_icon").value();
        entries.push_back(e);
        const std::string group = menu->get_string("group").value();
        for (Group& g : groups)
            if (g.title == group) g.apps.push_back(e.id);
        sync_view();
    }

    void add_data_dir(const std::string& name, const std::map<std::string, std::string>& content) {
        dirs.insert("system/" + name);
        for (const auto& kv : content) put_file("system/" + name + "/" + kv.first, kv.second);
    }

    void add_record(const std::string& app_id, const std::string& json_text) {
        put_file("system/pbstore/installed/" + app_id + ".json", json_text);
    }

    void put_file(const std::string& path, const std::string& data) {
        for (size_t i = 0; i < path.size(); ++i)
            if (path[i] == '/') dirs.insert(path.substr(0, i));
        files[path] = data;
    }

    // Replaces the content by what a volume holds (for example after an installation was run on it).
    void assign_tree(const Tree& t) {
        files = t.files;
        dirs = t.dirs;
    }

    Tree tree() const {
        Tree t;
        t.files = files;
        t.dirs = dirs;
        return t;
    }
    void load_into(CrashFs& fs) const { fs.load_tree(tree()); }

    void write_to(const std::string& root) const {
        ::mkdir(root.c_str(), 0755);
        std::vector<std::string> sorted(dirs.begin(), dirs.end());
        for (const std::string& d : sorted) ::mkdir((root + "/" + d).c_str(), 0755);   // parents sort before children
        for (const auto& kv : files) {
            std::ofstream f(root + "/" + kv.first, std::ios::binary | std::ios::trunc);
            f << kv.second;
        }
    }

private:
    std::string unit() const { return style_ == Tabs ? "\t" : style_ == Pretty4 ? "    " : "  "; }
    std::string indent(int level) const {
        std::string s;
        for (int i = 0; i < level; ++i) s += unit();
        return s;
    }
    void emit(const pbstore::json::Value& v, int level, std::string* out) const {
        using pbstore::json::Value;
        const bool mini = style_ == Minified;
        if (v.is_object()) {
            if (v.object.empty()) { *out += "{}"; return; }
            *out += mini ? "{" : "{\n";
            for (size_t i = 0; i < v.object.size(); ++i) {
                if (!mini) *out += indent(level + 1);
                *out += pbstore::json::quote(v.object[i].first) + (mini ? ":" : ": ");
                emit(v.object[i].second, level + 1, out);
                if (i + 1 < v.object.size()) *out += mini ? "," : ",\n";
                else if (!mini) *out += "\n";
            }
            if (!mini) *out += indent(level);
            *out += "}";
        } else if (v.is_array()) {
            if (v.array.empty()) { *out += "[]"; return; }
            bool scalars = true;
            for (const Value& e : v.array) scalars = scalars && !e.is_object() && !e.is_array();
            if (scalars || mini) {
                *out += "[";
                for (size_t i = 0; i < v.array.size(); ++i) {
                    if (i) *out += mini ? "," : ", ";
                    emit(v.array[i], level + 1, out);
                }
                *out += "]";
            } else {
                *out += "[\n";
                for (size_t i = 0; i < v.array.size(); ++i) {
                    *out += indent(level + 1);
                    emit(v.array[i], level + 1, out);
                    *out += i + 1 < v.array.size() ? ",\n" : "\n";
                }
                *out += indent(level) + "]";
            }
        } else {
            std::string s = pbstore::json::write(v);
            while (!s.empty() && s.back() == '\n') s.pop_back();
            *out += s;
        }
    }

    Style style_;
};

}  // namespace pbtest
