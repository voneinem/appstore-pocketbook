#include "pbstore/install/install_record.h"

#include <cctype>
#include <set>

#include "pbstore/install/reader_fs.h"
#include "pbstore/json.h"
#include "pbstore/semver.h"
#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

namespace {

bool valid_app_id(const std::string& s) {
    if (s.size() < 2 || s.size() > 40 || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

bool valid_written_at(const std::string& s) {
    static const char* shape = "dddd-dd-ddTdd:dd:ddZ";
    if (s.size() != 20) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (shape[i] == 'd' ? !std::isdigit(static_cast<unsigned char>(s[i])) : s[i] != shape[i]) return false;
    }
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool under_applications(const std::string& p) {
    return p.compare(0, 13, "applications/") == 0 && p.size() > 13 && valid_relative_path(p);
}

bool valid_data_dir(const std::string& s) {
    if (s.compare(0, 7, "system/") != 0 || s.size() < 9) return false;
    const std::string name = s.substr(7);
    if (name == "pbstore") return false;
    if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9'))) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

RecordParse refuse(const std::string& app_id, const std::string& why) {
    RecordParse r;
    r.findings.push_back(make_error("E-INS-35", "system/pbstore/installed/" + (app_id.empty() ? std::string("?") : app_id) + ".json", why));
    return r;
}

}  // namespace

std::string record_path(const std::string& app_id) { return "system/pbstore/installed/" + app_id + ".json"; }

RecordParse parse_install_record(const std::string& text, const std::string& expected_app_id) {
    const std::string who = expected_app_id;
    if (text.size() > kMaxRecordBytes) return refuse(who, "the install record is larger than 256 KiB");
    std::string err;
    auto parsed = json::parse(text, &err, kMaxRecordBytes);
    if (!parsed || !parsed->is_object()) return refuse(who, "the install record is not a valid JSON object");
    const json::Value& v = *parsed;

    const auto schema = v.get_string("schema_version");
    if (!schema) return refuse(who, "the install record has no schema_version");
    {
        const size_t dot = schema->find('.');
        bool ok = dot != std::string::npos && dot > 0 && dot + 1 < schema->size() && schema->size() < 12;
        for (size_t i = 0; ok && i < schema->size(); ++i)
            if (i != dot && !std::isdigit(static_cast<unsigned char>((*schema)[i]))) ok = false;
        if (!ok) return refuse(who, "the install record has a malformed schema_version \"" + *schema + "\"");
        if (std::stoi(schema->substr(0, dot)) != 1)
            return refuse(who, "the install record is from a newer format (schema " + *schema + "); the installer needs updating");
    }

    InstallRecord r;
    r.schema_version = *schema;
    auto str = [&](const char* key, std::string* out) {
        auto s = v.get_string(key);
        if (!s) return false;
        *out = *s;
        return true;
    };
    if (!str("app_id", &r.app_id) || !valid_app_id(r.app_id)) return refuse(who, "the install record has no valid app_id");
    if (!expected_app_id.empty() && r.app_id != expected_app_id) return refuse(who, "the app_id \"" + r.app_id + "\" differs from the file name");
    if (!str("version", &r.version) || !parse_semver(r.version)) return refuse(who, "the install record has no valid version");
    if (!str("source", &r.source) || (r.source != "catalog" && r.source != "local" && r.source != "adopted"))
        return refuse(who, "the install record has no valid source");
    if (!str("package_sha256", &r.package_sha256) || !is_sha256_hex(r.package_sha256)) return refuse(who, "the install record has no valid package_sha256");
    if (!str("entry_sha256", &r.entry_sha256) || !is_sha256_hex(r.entry_sha256)) return refuse(who, "the install record has no valid entry_sha256");
    if (!str("written_at", &r.written_at) || !valid_written_at(r.written_at)) return refuse(who, "the install record has no valid written_at");
    if (!str("installed_by", &r.installed_by) || r.installed_by.empty()) return refuse(who, "the install record has no installed_by");

    const json::Value* files = v.find("files");
    if (!files || !files->is_array()) return refuse(who, "the install record has no list of files");
    std::set<std::string> seen;
    for (const json::Value& f : files->array) {
        if (!f.is_object()) return refuse(who, "a file entry of the install record is not an object");
        FileEntry e;
        const auto path = f.get_string("path");
        const auto size = f.get_int("size");
        const auto sha = f.get_string("sha256");
        if (!path || !under_applications(*path)) return refuse(who, "a file path of the install record is not under applications/");
        if (!size || *size < 0) return refuse(who, "a file size of the install record is not valid");
        if (!sha || !is_sha256_hex(*sha)) return refuse(who, "a file hash of the install record is not valid");
        if (!seen.insert(lower(*path)).second) return refuse(who, "the install record lists \"" + *path + "\" twice (ignoring letter case)");
        e.path = *path;
        e.size = *size;
        e.sha256 = *sha;
        r.files.push_back(e);
    }

    const json::Value* dirs = v.find("directories");
    if (!dirs || !dirs->is_array()) return refuse(who, "the install record has no list of directories");
    for (const json::Value& d : dirs->array) {
        if (!d.is_string() || !under_applications(d.string)) return refuse(who, "a directory of the install record is not under applications/");
        r.directories.push_back(d.string);
    }

    const json::Value* menu = v.find("menu_entry");
    if (!menu || !menu->is_object()) return refuse(who, "the install record has no menu_entry");
    for (const char* key : {"id", "title", "path", "icon", "focused_icon", "group"}) {
        auto s = menu->get_string(key);
        if (!s) return refuse(who, std::string("the menu_entry of the install record lacks \"") + key + "\"");
    }
    r.menu_entry.id = *menu->get_string("id");
    r.menu_entry.title = *menu->get_string("title");
    r.menu_entry.path = *menu->get_string("path");
    r.menu_entry.icon = *menu->get_string("icon");
    r.menu_entry.focused_icon = *menu->get_string("focused_icon");
    r.menu_entry.group = *menu->get_string("group");
    if (r.menu_entry.id.empty() || r.menu_entry.path.empty()) return refuse(who, "the menu_entry of the install record is incomplete");

    const json::Value* dd = v.find("data_dir");
    if (!dd) return refuse(who, "the install record has no data_dir");
    if (dd->is_string()) {
        if (!valid_data_dir(dd->string)) return refuse(who, "the data_dir of the install record is not valid");
        r.data_dir = dd->string;
    } else if (!dd->is_null()) {
        return refuse(who, "the data_dir of the install record is not valid");
    }

    RecordParse ok;
    ok.record = std::move(r);
    return ok;
}

std::string write_install_record(const InstallRecord& r) {
    using json::Value;
    Value v = Value::make_object();
    v.set("schema_version", Value::from_string(r.schema_version));
    v.set("app_id", Value::from_string(r.app_id));
    v.set("version", Value::from_string(r.version));
    v.set("source", Value::from_string(r.source));
    v.set("package_sha256", Value::from_string(r.package_sha256));
    Value files = Value::make_array();
    for (const FileEntry& f : r.files) {
        Value o = Value::make_object();
        o.set("path", Value::from_string(f.path));
        o.set("size", Value::from_int(f.size));
        o.set("sha256", Value::from_string(f.sha256));
        files.push(o);
    }
    v.set("files", files);
    Value dirs = Value::make_array();
    for (const std::string& d : r.directories) dirs.push(Value::from_string(d));
    v.set("directories", dirs);
    Value menu = Value::make_object();
    menu.set("id", Value::from_string(r.menu_entry.id));
    menu.set("title", Value::from_string(r.menu_entry.title));
    menu.set("path", Value::from_string(r.menu_entry.path));
    menu.set("icon", Value::from_string(r.menu_entry.icon));
    menu.set("focused_icon", Value::from_string(r.menu_entry.focused_icon));
    menu.set("group", Value::from_string(r.menu_entry.group));
    v.set("menu_entry", menu);
    v.set("entry_sha256", Value::from_string(r.entry_sha256));
    v.set("data_dir", r.data_dir ? Value::from_string(*r.data_dir) : Value());
    v.set("written_at", Value::from_string(r.written_at));
    v.set("installed_by", Value::from_string(r.installed_by));
    return json::write(v);
}

}  // namespace install
}  // namespace pbstore
