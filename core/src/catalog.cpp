#include "pbstore/catalog.h"

#include <algorithm>
#include <map>
#include <set>

#include "pbstore/firmware_version.h"
#include "pbstore/json.h"
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

class Checker {
public:
    explicit Checker(std::vector<Finding>* out) : out_(out) {}

    void bad(const std::string& path, const std::string& message) {
        out_->push_back(make_error("E-CAT-02", "catalog", path + ": " + message));
        ++count_;
    }
    size_t count() const { return count_; }

    const Value* member(const Value& obj, const std::string& path, const std::string& key) {
        const Value* v = obj.find(key);
        if (!v) bad(path + key, "required field is missing");
        return v;
    }
    bool string_field(const Value& obj, const std::string& path, const std::string& key, std::string* out) {
        const Value* v = member(obj, path, key);
        if (!v) return false;
        if (!v->is_string()) { bad(path + key, "must be a string"); return false; }
        *out = v->string;
        return true;
    }
    bool int_field(const Value& obj, const std::string& path, const std::string& key, int64_t* out) {
        const Value* v = member(obj, path, key);
        if (!v) return false;
        if (!v->is_number() || !v->is_integer) { bad(path + key, "must be an integer"); return false; }
        *out = static_cast<int64_t>(v->number);
        return true;
    }
    bool text_field(const Value& obj, const std::string& path, const std::string& key, size_t max_len, LocalizedText* out) {
        const Value* v = member(obj, path, key);
        if (!v) return false;
        if (!v->is_object()) { bad(path + key, "must be an object with an \"en\" text"); return false; }
        const std::string p = path + key;
        const Value* en = v->find("en");
        if (!en || !en->is_string()) { bad(p + ".en", "required text is missing"); return false; }
        if (!validate::text_length_between(en->string, 1, max_len)) {
            bad(p + ".en", "must be valid UTF-8 text of 1 to " + std::to_string(max_len) + " characters");
            return false;
        }
        out->en = en->string;
        if (const Value* de = v->find("de")) {
            if (!de->is_string() || !validate::text_length_between(de->string, 1, max_len)) {
                bad(p + ".de", "must be valid UTF-8 text of 1 to " + std::to_string(max_len) + " characters");
                return false;
            }
            out->de = de->string;
        }
        return true;
    }
    bool version_list(const Value& obj, const std::string& path, const std::string& key, std::vector<std::string>* out) {
        const Value* v = member(obj, path, key);
        if (!v) return false;
        if (!v->is_array()) { bad(path + key, "must be an array of versions"); return false; }
        bool ok = true;
        for (size_t i = 0; i < v->array.size(); ++i) {
            const Value& e = v->array[i];
            if (!e.is_string() || !parse_semver(e.string)) {
                bad(path + key + "[" + std::to_string(i) + "]", "must be a semantic version like 1.2.3");
                ok = false;
            } else {
                out->push_back(e.string);
            }
        }
        return ok;
    }

private:
    std::vector<Finding>* out_;
    size_t count_ = 0;
};

void parse_entry_object(const Value& v, const std::string& path, Checker& c, CatalogEntry* e) {
    if (!v.is_object()) { c.bad(path.substr(0, path.size() - 1), "must be an object"); return; }
    std::string s;
    if (c.string_field(v, path, "id", &s)) {
        if (!validate::is_app_id(s)) c.bad(path + "id", "must match ^[a-z][a-z0-9-]{1,39}$ (got " + shown(s) + ")");
        e->id = s;
    }
    if (c.string_field(v, path, "name", &s)) {
        if (!validate::text_length_between(s, 1, 40)) c.bad(path + "name", "must be 1 to 40 characters (got " + shown(s) + ")");
        e->name = s;
    }
    c.text_field(v, path, "short_description", 120, &e->short_description);
    c.text_field(v, path, "long_description", 2000, &e->long_description);
    if (c.string_field(v, path, "version", &s)) {
        if (!parse_semver(s)) c.bad(path + "version", "must be a semantic version like 1.2.3 (got " + shown(s) + ")");
        e->version = s;
    }
    if (c.string_field(v, path, "release_date", &s)) {
        if (!validate::is_iso_date(s)) c.bad(path + "release_date", "must be a date YYYY-MM-DD (got " + shown(s) + ")");
        e->release_date = s;
    }
    if (c.string_field(v, path, "package_url", &s)) {
        if (!validate::parse_github_release_url(s))
            c.bad(path + "package_url", "must be https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.zip (got " + shown(s) + ")");
        e->package_url = s;
    }
    int64_t n = 0;
    if (c.int_field(v, path, "package_size", &n)) {
        if (n < 1 || n > kMaxPackageBytes) c.bad(path + "package_size", "must be 1 to 67108864 bytes (got " + std::to_string(n) + ")");
        e->package_size = n;
    }
    if (c.string_field(v, path, "package_sha256", &s)) {
        if (!is_sha256_hex(s)) c.bad(path + "package_sha256", "must be 64 lowercase hex characters (got " + shown(s) + ")");
        e->package_sha256 = s;
    }
    if (c.string_field(v, path, "license", &s)) {
        if (s.empty() || s.size() > 64 || !validate::is_valid_utf8(s) || s.find_first_of(" \t\r\n") != std::string::npos)
            c.bad(path + "license", "must be a license identifier without spaces, at most 64 characters (got " + shown(s) + ")");
        e->license = s;
    }
    if (c.string_field(v, path, "source_url", &s)) {
        if (!validate::is_https_url(s)) c.bad(path + "source_url", "must be an https:// URL (got " + shown(s) + ")");
        e->source_url = s;
    }
    if (c.string_field(v, path, "author", &s)) {
        if (!validate::text_length_between(s, 1, 80)) c.bad(path + "author", "must be 1 to 80 characters (got " + shown(s) + ")");
        e->author = s;
    }
    if (c.string_field(v, path, "min_firmware", &s)) {
        if (!parse_firmware(s)) c.bad(path + "min_firmware", "must be dotted decimal numbers like 6.8.4143 (got " + shown(s) + ")");
        e->min_firmware = s;
    }
    if (c.string_field(v, path, "target_device", &s)) {
        if (s != "InkPad One") c.bad(path + "target_device", "must be \"InkPad One\" (got " + shown(s) + ")");
        e->target_device = s;
    }
    if (c.string_field(v, path, "icon_url", &s)) {
        if (!validate::is_https_url(s)) c.bad(path + "icon_url", "must be an https:// URL (got " + shown(s) + ")");
        e->icon_url = s;
    }
    if (c.string_field(v, path, "changelog_url", &s)) {
        if (!validate::is_https_url(s)) c.bad(path + "changelog_url", "must be an https:// URL (got " + shown(s) + ")");
        e->changelog_url = s;
    }
    c.version_list(v, path, "withdrawn_versions", &e->withdrawn_versions);
    if (c.version_list(v, path, "listed_versions", &e->listed_versions)) {
        bool ascending = true;
        for (size_t i = 1; i < e->listed_versions.size(); ++i) {
            auto a = parse_semver(e->listed_versions[i - 1]), b = parse_semver(e->listed_versions[i]);
            if (a && b && compare(*a, *b) >= 0) ascending = false;
        }
        if (!ascending) c.bad(path + "listed_versions", "must be strictly ascending without duplicates");
        if (!e->version.empty() &&
            std::find(e->listed_versions.begin(), e->listed_versions.end(), e->version) == e->listed_versions.end())
            c.bad(path + "listed_versions", "must contain the listed version " + shown(e->version));
    }
    if (c.string_field(v, path, "menu_entry_id", &s)) {
        if (!validate::is_menu_entry_id(s)) c.bad(path + "menu_entry_id", "must match ^U_[A-Za-z0-9_]{1,40}$ (got " + shown(s) + ")");
        e->menu_entry_id = s;
    }
    if (const Value* d = c.member(v, path, "data_dir")) {
        if (d->is_null()) {
            e->data_dir.reset();
        } else if (!d->is_string() || !validate::is_data_dir(d->string)) {
            c.bad(path + "data_dir", "must be null or system/<name> with <name> matching ^[a-z0-9][a-z0-9_-]{1,39}$");
        } else {
            e->data_dir = d->string;
        }
    }
}

}  // namespace

CatalogParseResult parse_catalog(const std::string& bytes) {
    CatalogParseResult result;
    std::string err;
    auto doc = json::parse(bytes, &err);
    if (!doc) {
        result.findings.push_back(make_error("E-CAT-02", "catalog", "not valid JSON: " + err));
        return result;
    }
    if (!doc->is_object()) {
        result.findings.push_back(make_error("E-CAT-02", "catalog", "the document must be a JSON object"));
        return result;
    }
    Checker c(&result.findings);
    Catalog cat;
    if (!c.string_field(*doc, "", "schema_version", &cat.schema_version)) return result;
    auto sv = parse_schema_version(cat.schema_version);
    if (!sv) {
        c.bad("schema_version", "must be \"MAJOR.MINOR\" (got " + shown(cat.schema_version) + ")");
        return result;
    }
    switch (schema_support(*sv)) {
        case SchemaSupport::NeedsUpdate:
            result.findings.push_back(make_error("E-CAT-03", "catalog",
                "catalog schema " + cat.schema_version + " is newer than this program supports (" +
                std::to_string(kSupportedSchemaMajor) + ".x): this program needs updating"));
            return result;
        case SchemaSupport::Unsupported:
            c.bad("schema_version", "schema " + cat.schema_version + " is older than this program supports");
            return result;
        case SchemaSupport::Supported: break;
    }
    int64_t serial = 0;
    if (c.int_field(*doc, "", "serial", &serial)) {
        if (serial < 1) c.bad("serial", "must be an integer of at least 1 (got " + std::to_string(serial) + ")");
        cat.serial = serial;
    }
    if (c.string_field(*doc, "", "generated_at", &cat.generated_at)) {
        if (!validate::is_iso_timestamp(cat.generated_at))
            c.bad("generated_at", "must be YYYY-MM-DDTHH:MM:SSZ (got " + shown(cat.generated_at) + ")");
    }
    if (const Value* apps = c.member(*doc, "", "apps")) {
        if (!apps->is_array()) {
            c.bad("apps", "must be an array");
        } else {
            std::map<std::string, size_t> ids, menus, dirs;
            for (size_t i = 0; i < apps->array.size(); ++i) {
                const std::string path = "apps[" + std::to_string(i) + "].";
                CatalogEntry e;
                parse_entry_object(apps->array[i], path, c, &e);
                if (!e.id.empty()) {
                    if (!cat.apps.empty() && !(cat.apps.back().id < e.id) && cat.apps.back().id != e.id)
                        c.bad("apps", "must be sorted by id (" + shown(e.id) + " comes after " + shown(cat.apps.back().id) + ")");
                    auto it = ids.find(e.id);
                    if (it != ids.end()) c.bad(path + "id", "duplicate id " + shown(e.id) + " (also apps[" + std::to_string(it->second) + "])");
                    ids.emplace(e.id, i);
                }
                if (!e.menu_entry_id.empty()) {
                    auto it = menus.find(e.menu_entry_id);
                    if (it != menus.end()) c.bad(path + "menu_entry_id", "duplicate " + shown(e.menu_entry_id) + " (also apps[" + std::to_string(it->second) + "])");
                    menus.emplace(e.menu_entry_id, i);
                }
                if (e.data_dir) {
                    auto it = dirs.find(*e.data_dir);
                    if (it != dirs.end()) c.bad(path + "data_dir", "duplicate " + shown(*e.data_dir) + " (also apps[" + std::to_string(it->second) + "])");
                    dirs.emplace(*e.data_dir, i);
                }
                cat.apps.push_back(std::move(e));
            }
        }
    }
    if (c.count() == 0) result.catalog = std::move(cat);
    return result;
}

namespace {

Value text_value(const LocalizedText& t) {
    Value o = Value::make_object();
    o.set("en", Value::from_string(t.en));
    if (t.de) o.set("de", Value::from_string(*t.de));
    return o;
}

Value versions_value(const std::vector<std::string>& v) {
    Value a = Value::make_array();
    for (const std::string& s : v) a.push(Value::from_string(s));
    return a;
}

Value entry_value(const CatalogEntry& e) {
    Value o = Value::make_object();
    o.set("id", Value::from_string(e.id));
    o.set("name", Value::from_string(e.name));
    o.set("short_description", text_value(e.short_description));
    o.set("long_description", text_value(e.long_description));
    o.set("version", Value::from_string(e.version));
    o.set("release_date", Value::from_string(e.release_date));
    o.set("package_url", Value::from_string(e.package_url));
    o.set("package_size", Value::from_int(e.package_size));
    o.set("package_sha256", Value::from_string(e.package_sha256));
    o.set("license", Value::from_string(e.license));
    o.set("source_url", Value::from_string(e.source_url));
    o.set("author", Value::from_string(e.author));
    o.set("min_firmware", Value::from_string(e.min_firmware));
    o.set("target_device", Value::from_string(e.target_device));
    o.set("icon_url", Value::from_string(e.icon_url));
    o.set("changelog_url", Value::from_string(e.changelog_url));
    o.set("withdrawn_versions", versions_value(e.withdrawn_versions));
    o.set("listed_versions", versions_value(e.listed_versions));
    o.set("menu_entry_id", Value::from_string(e.menu_entry_id));
    o.set("data_dir", e.data_dir ? Value::from_string(*e.data_dir) : Value());
    return o;
}

}  // namespace

std::string write_catalog(const Catalog& catalog) {
    std::vector<CatalogEntry> apps = catalog.apps;
    std::stable_sort(apps.begin(), apps.end(), [](const CatalogEntry& a, const CatalogEntry& b) { return a.id < b.id; });
    Value root = Value::make_object();
    root.set("schema_version", Value::from_string(catalog.schema_version));
    root.set("serial", Value::from_int(catalog.serial));
    root.set("generated_at", Value::from_string(catalog.generated_at));
    Value list = Value::make_array();
    for (const CatalogEntry& e : apps) list.push(entry_value(e));
    root.set("apps", list);
    return json::write(root);
}

bool is_withdrawn(const CatalogEntry& entry, const std::string& version) {
    return std::find(entry.withdrawn_versions.begin(), entry.withdrawn_versions.end(), version) !=
           entry.withdrawn_versions.end();
}

bool offered(const CatalogEntry& entry) { return !is_withdrawn(entry, entry.version); }

}  // namespace pbstore
