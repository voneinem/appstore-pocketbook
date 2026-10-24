#include "pbstore/manifest.h"

#include <set>

#include "pbstore/firmware_version.h"
#include "pbstore/json.h"
#include "pbstore/reserved_dirs.h"
#include "pbstore/schema.h"
#include "pbstore/semver.h"
#include "pbstore/sha256.h"
#include "pbstore/validate.h"

namespace pbstore {

namespace {

using json::Value;

std::string shown(const std::string& s) {
    std::string t = s.size() > 60 ? s.substr(0, 57) + "..." : s;
    for (char& c : t)
        if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    return "\"" + t + "\"";
}

std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

class Reader {
public:
    explicit Reader(std::vector<Finding>* out) : out_(out) {}
    void bad(const char* rule, const std::string& path, const std::string& message) {
        out_->push_back(make_error(rule, "manifest.json", path + ": " + message));
        ++count_;
    }
    size_t count() const { return count_; }
    const Value* member(const Value& obj, const std::string& path, const std::string& key) {
        const Value* v = obj.find(key);
        if (!v) bad("E-PKG-07", path + key, "required field is missing");
        return v;
    }
    bool string_field(const Value& obj, const std::string& path, const std::string& key, std::string* out) {
        const Value* v = member(obj, path, key);
        if (!v) return false;
        if (!v->is_string()) { bad("E-PKG-07", path + key, "must be a string"); return false; }
        *out = v->string;
        return true;
    }

private:
    std::vector<Finding>* out_;
    size_t count_ = 0;
};

bool no_control_chars(const std::string& s) {
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F) return false;
    return true;
}

}  // namespace

ManifestParseResult parse_manifest(const std::string& bytes) {
    ManifestParseResult result;
    auto fail5 = [&](const std::string& message) {
        result.findings.push_back(make_error("E-PKG-05", "manifest.json", message));
        return result;
    };
    if (bytes.size() > kMaxManifestBytes)
        return fail5("manifest.json is " + std::to_string(bytes.size()) + " bytes, the limit is " + std::to_string(kMaxManifestBytes));
    std::string err;
    auto doc = json::parse(bytes, &err, kMaxManifestBytes);
    if (!doc) return fail5("not valid JSON: " + err);
    if (!doc->is_object()) return fail5("the manifest must be a JSON object");

    Reader r(&result.findings);
    Manifest m;
    // schema version first: a newer major is not judged any further
    if (!r.string_field(*doc, "", "schema_version", &m.schema_version)) return result;
    auto sv = parse_schema_version(m.schema_version);
    if (!sv) {
        r.bad("E-PKG-06", "schema_version", "must be \"MAJOR.MINOR\" (got " + shown(m.schema_version) + ")");
        return result;
    }
    switch (schema_support(*sv)) {
        case SchemaSupport::NeedsUpdate:
            r.bad("E-PKG-06", "schema_version", "manifest schema " + m.schema_version + " is newer than this program supports (" +
                  std::to_string(kSupportedSchemaMajor) + ".x): this program needs updating");
            return result;
        case SchemaSupport::Unsupported:
            r.bad("E-PKG-06", "schema_version", "schema " + m.schema_version + " is older than this program supports");
            return result;
        case SchemaSupport::Supported: break;
    }

    // identity
    if (r.string_field(*doc, "", "id", &m.id) && !validate::is_app_id(m.id))
        r.bad("E-PKG-08", "id", "must match ^[a-z][a-z0-9-]{1,39}$ (got " + shown(m.id) + ")");
    if (r.string_field(*doc, "", "name", &m.name) && !validate::text_length_between(m.name, 1, 40))
        r.bad("E-PKG-08", "name", "must be 1 to 40 characters (got " + shown(m.name) + ")");
    if (r.string_field(*doc, "", "version", &m.version) && !parse_semver(m.version))
        r.bad("E-PKG-08", "version", "must be a semantic version like 1.2.3 (got " + shown(m.version) + ")");
    if (r.string_field(*doc, "", "min_firmware", &m.min_firmware) && !parse_firmware(m.min_firmware))
        r.bad("E-PKG-08", "min_firmware", "must be dotted decimal numbers like 6.8.4143 (got " + shown(m.min_firmware) + ")");
    if (r.string_field(*doc, "", "target_device", &m.target_device) && m.target_device != "InkPad One")
        r.bad("E-PKG-08", "target_device", "must be \"InkPad One\" (got " + shown(m.target_device) + ")");

    // files
    bool files_ok = false;
    if (const Value* files = r.member(*doc, "", "files")) {
        if (!files->is_array()) {
            r.bad("E-PKG-07", "files", "must be an array");
        } else if (files->array.empty()) {
            r.bad("E-PKG-07", "files", "must list at least one file");
        } else {
            const size_t before = r.count();
            std::set<std::string> lower_seen;
            for (size_t i = 0; i < files->array.size(); ++i) {
                const std::string p = "files[" + std::to_string(i) + "].";
                const Value& f = files->array[i];
                if (!f.is_object()) { r.bad("E-PKG-07", p.substr(0, p.size() - 1), "must be an object with path, size and sha256"); continue; }
                FileEntry e;
                const bool has_path = r.string_field(f, p, "path", &e.path);
                const Value* size = r.member(f, p, "size");
                const bool has_size = size && size->is_number() && size->is_integer;
                if (size && !has_size) r.bad("E-PKG-07", p + "size", "must be an integer");
                const bool has_sha = r.string_field(f, p, "sha256", &e.sha256);
                if (has_path) {
                    static const std::string prefix = "applications/";
                    if (e.path.compare(0, prefix.size(), prefix) != 0 || e.path.size() == prefix.size() || !validate::is_safe_path(e.path))
                        r.bad("E-PKG-09", p + "path", "must be a safe path inside applications/ (got " + shown(e.path) + ")");
                    else if (!lower_seen.insert(lower_ascii(e.path)).second)
                        r.bad("E-PKG-09", p + "path", "duplicate path " + shown(e.path) + " (paths must differ by more than letter case)");
                }
                if (has_size) {
                    e.size = static_cast<int64_t>(size->number);
                    if (e.size < 0) r.bad("E-PKG-09", p + "size", "must not be negative (got " + std::to_string(e.size) + ")");
                }
                if (has_sha && !is_sha256_hex(e.sha256))
                    r.bad("E-PKG-09", p + "sha256", "must be 64 lowercase hex characters (got " + shown(e.sha256) + ")");
                m.files.push_back(std::move(e));
            }
            files_ok = r.count() == before;
        }
    }

    // menu entry
    MenuEntry& me = m.menu_entry;
    bool menu_strings_ok = false;
    if (const Value* menu = r.member(*doc, "", "menu_entry")) {
        if (!menu->is_object()) {
            r.bad("E-PKG-07", "menu_entry", "must be an object");
        } else {
            const size_t before = r.count();
            const bool a = r.string_field(*menu, "menu_entry.", "id", &me.id);
            const bool b = r.string_field(*menu, "menu_entry.", "title", &me.title);
            const bool c = r.string_field(*menu, "menu_entry.", "path", &me.path);
            const bool d = r.string_field(*menu, "menu_entry.", "icon", &me.icon);
            const bool e = r.string_field(*menu, "menu_entry.", "focused_icon", &me.focused_icon);
            const bool f = r.string_field(*menu, "menu_entry.", "group", &me.group);
            if (a && !validate::is_menu_entry_id(me.id))
                r.bad("E-PKG-14", "menu_entry.id", "must match ^U_[A-Za-z0-9_]{1,40}$ (got " + shown(me.id) + ")");
            if (b && (!validate::text_length_between(me.title, 1, 40) || !no_control_chars(me.title)))
                r.bad("E-PKG-14", "menu_entry.title", "must be 1 to 40 characters (got " + shown(me.title) + ")");
            if (f && (me.group.size() < 2 || me.group[0] != '@' || !validate::text_length_between(me.group, 2, 40) || !no_control_chars(me.group)))
                r.bad("E-PKG-14", "menu_entry.group", "must start with @ followed by a name, 40 characters at most (got " + shown(me.group) + ")");
            menu_strings_ok = a && b && c && d && e && f && r.count() == before;
        }
    }
    // the menu paths must be among the listed files
    if (files_ok && !m.menu_entry.path.empty()) {
        std::set<std::string> listed;
        for (const FileEntry& f : m.files) listed.insert(f.path);
        auto check = [&](const char* field, const std::string& path) {
            if (!listed.count(path)) r.bad("E-PKG-13", std::string("menu_entry.") + field, shown(path) + " is not among the listed files");
        };
        check("path", me.path);
        check("icon", me.icon);
        check("focused_icon", me.focused_icon);
    }
    (void)menu_strings_ok;

    // data folder
    if (const Value* d = r.member(*doc, "", "data_dir")) {
        if (d->is_null()) {
            m.data_dir.reset();
        } else if (!d->is_string()) {
            r.bad("E-PKG-07", "data_dir", "must be a string or null");
        } else if (!validate::is_data_dir(d->string)) {
            r.bad("E-PKG-15", "data_dir", "must be system/<name> with <name> matching ^[a-z0-9][a-z0-9_-]{1,39}$ and never inside applications/ (got " + shown(d->string) + ")");
        } else if (is_reserved_data_dir(d->string.substr(7))) {
            r.bad("E-PKG-15", "data_dir", shown(d->string) + " is a reserved system folder name and cannot be an app's data folder");
        } else {
            m.data_dir = d->string;
        }
    }

    if (r.count() == 0) result.manifest = std::move(m);
    return result;
}

}  // namespace pbstore
