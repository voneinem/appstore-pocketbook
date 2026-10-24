#include "pbstore/entry.h"

#include <algorithm>
#include <set>

#include "pbstore/json.h"
#include "pbstore/schema.h"
#include "pbstore/semver.h"
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

const std::vector<std::string>& accepted_licenses() {
    static const std::vector<std::string> ids = {
#include "accepted_licenses_data.inc"
    };
    return ids;
}

class Checker {
public:
    Checker(std::vector<Finding>* out, std::string subject) : out_(out), subject_(std::move(subject)) {}
    void bad(const char* rule, const std::string& message) {
        out_->push_back(make_error(rule, subject_, message));
        ++errors_;
    }
    void notice(const char* rule, const std::string& message) { out_->push_back(make_notice(rule, subject_, message)); }
    size_t errors() const { return errors_; }

    const Value* member(const Value& obj, const std::string& key) {
        const Value* v = obj.find(key);
        if (!v) bad("E-ENT-02", key + ": required field is missing");
        return v;
    }
    bool string_field(const Value& obj, const std::string& key, std::string* out) {
        const Value* v = member(obj, key);
        if (!v) return false;
        if (!v->is_string()) { bad("E-ENT-02", key + ": must be a string"); return false; }
        *out = v->string;
        return true;
    }
    void text_field(const Value& obj, const std::string& key, size_t max_len, LocalizedText* out) {
        const Value* v = member(obj, key);
        if (!v) return;
        if (!v->is_object()) { bad("E-ENT-02", key + ": must be an object with an \"en\" text"); return; }
        const Value* en = v->find("en");
        if (!en || !en->is_string() || !validate::text_length_between(en->string, 1, max_len)) {
            bad("E-ENT-08", key + ".en: the English text is required, 1 to " + std::to_string(max_len) + " characters of valid UTF-8" +
                (en && en->is_string() ? " (got " + std::to_string(validate::utf8_length(en->string)) + ")" : ""));
            return;
        }
        out->en = en->string;
        if (const Value* de = v->find("de")) {
            if (!de->is_string() || !validate::text_length_between(de->string, 1, max_len)) {
                bad("E-ENT-08", key + ".de: if given, 1 to " + std::to_string(max_len) + " characters of valid UTF-8");
                return;
            }
            out->de = de->string;
        }
    }

private:
    std::vector<Finding>* out_;
    std::string subject_;
    size_t errors_ = 0;
};

std::string stem_of(const std::string& file_name) {
    const size_t dot = file_name.rfind('.');
    return dot == std::string::npos || dot == 0 ? file_name : file_name.substr(0, dot);
}

}  // namespace

bool is_accepted_license(const std::string& spdx_id) {
    const auto& ids = accepted_licenses();
    return std::find(ids.begin(), ids.end(), spdx_id) != ids.end();
}

std::string next_day(const std::string& iso_date) {
    if (!validate::is_iso_date(iso_date)) return "";
    int y = std::stoi(iso_date.substr(0, 4)), m = std::stoi(iso_date.substr(5, 2)), d = std::stoi(iso_date.substr(8, 2));
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    const int max = days[m - 1] + ((m == 2 && leap) ? 1 : 0);
    if (++d > max) { d = 1; if (++m > 12) { m = 1; ++y; } }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
    return buf;
}

bool looks_like_email(const std::string& text) {
    auto local_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-'; };
    auto domain_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-'; };
    for (size_t at = text.find('@'); at != std::string::npos; at = text.find('@', at + 1)) {
        if (at == 0 || !local_char(text[at - 1])) continue;
        size_t end = at + 1;
        while (end < text.size() && domain_char(text[end])) ++end;
        std::string domain = text.substr(at + 1, end - at - 1);
        while (!domain.empty() && domain.back() == '.') domain.pop_back();
        const size_t dot = domain.rfind('.');
        if (dot == std::string::npos || dot == 0 || domain.size() - dot - 1 < 2) continue;
        bool letters = true;
        for (size_t i = dot + 1; i < domain.size(); ++i) letters = letters && std::isalpha(static_cast<unsigned char>(domain[i]));
        if (letters) return true;
    }
    return false;
}

EntryParseResult parse_entry(const std::string& bytes, const std::string& file_name, const std::string& today) {
    EntryParseResult result;
    const std::string subject = stem_of(file_name);
    Checker c(&result.findings, subject);
    if (bytes.size() > kMaxEntryBytes) {
        c.bad("E-ENT-01", "the file is " + std::to_string(bytes.size()) + " bytes, the limit is " + std::to_string(kMaxEntryBytes));
        return result;
    }
    std::string err;
    auto doc = json::parse(bytes, &err, kMaxEntryBytes);
    if (!doc) { c.bad("E-ENT-01", "not valid JSON: " + err); return result; }
    if (!doc->is_object()) { c.bad("E-ENT-01", "the entry must be a JSON object"); return result; }

    Entry e;
    if (!c.string_field(*doc, "schema_version", &e.schema_version)) return result;
    auto sv = parse_schema_version(e.schema_version);
    if (!sv) { c.bad("E-ENT-03", "schema_version: must be \"MAJOR.MINOR\" (got " + shown(e.schema_version) + ")"); return result; }
    switch (schema_support(*sv)) {
        case SchemaSupport::NeedsUpdate:
            c.bad("E-ENT-03", "schema_version: entry schema " + e.schema_version + " is newer than this program supports (" +
                  std::to_string(kSupportedSchemaMajor) + ".x): this program needs updating");
            return result;
        case SchemaSupport::Unsupported:
            c.bad("E-ENT-03", "schema_version: schema " + e.schema_version + " is older than this program supports");
            return result;
        case SchemaSupport::Supported: break;
    }

    for (const char* key : {"version", "min_firmware", "target_device", "name"})
        if (doc->find(key))
            c.bad("E-ENT-02", std::string(key) + ": this field must not be in an entry file; it comes from the package manifest");

    // id and file name
    if (c.string_field(*doc, "id", &e.id)) {
        if (!validate::is_app_id(e.id)) c.bad("E-ENT-04", "id: must match ^[a-z][a-z0-9-]{1,39}$ (got " + shown(e.id) + ")");
        else if (file_name != e.id + ".json") c.bad("E-ENT-04", "the file must be named " + e.id + ".json (got " + shown(file_name) + ")");
    }
    // package link
    std::optional<validate::GithubRelease> release;
    if (c.string_field(*doc, "package_url", &e.package_url)) {
        release = validate::parse_github_release_url(e.package_url);
        if (!release)
            c.bad("E-ENT-05", "package_url: must be https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.zip with no query or fragment (got " + shown(e.package_url) + ")");
    }
    // release date
    if (c.string_field(*doc, "release_date", &e.release_date)) {
        if (!validate::is_iso_date(e.release_date)) c.bad("E-ENT-09", "release_date: must be a date YYYY-MM-DD (got " + shown(e.release_date) + ")");
        else if (validate::is_iso_date(today) && e.release_date > next_day(today))
            c.bad("E-ENT-09", "release_date: " + e.release_date + " is in the future (today is " + today + " UTC; at most tomorrow is allowed)");
    }
    c.text_field(*doc, "short_description", 120, &e.short_description);
    c.text_field(*doc, "long_description", 2000, &e.long_description);
    // license
    if (const Value* l = doc->find("license")) {
        if (!l->is_string()) c.bad("E-ENT-06", "license: must be a license identifier string");
        else {
            e.license = l->string;
            if (!is_accepted_license(e.license))
                c.bad("E-ENT-06", "license: " + shown(e.license) + " is not on the accepted list of OSI-approved open-source licenses (catalog/accepted-licenses.txt); closed-source apps cannot be listed");
        }
    } else {
        c.bad("E-ENT-06", "license: required field is missing (an SPDX identifier of an accepted open-source license)");
    }
    // links
    if (c.string_field(*doc, "source_url", &e.source_url) && !validate::is_https_url(e.source_url))
        c.bad("E-ENT-07", "source_url: must be an https:// URL (got " + shown(e.source_url) + ")");
    if (c.string_field(*doc, "icon_url", &e.icon_url) && !validate::is_https_url(e.icon_url))
        c.bad("E-ENT-07", "icon_url: must be an https:// URL (got " + shown(e.icon_url) + ")");
    if (c.string_field(*doc, "changelog_url", &e.changelog_url) && !validate::is_https_url(e.changelog_url))
        c.bad("E-ENT-07", "changelog_url: must be an https:// URL (got " + shown(e.changelog_url) + ")");
    // author
    if (c.string_field(*doc, "author", &e.author) && !validate::text_length_between(e.author, 1, 80))
        c.bad("E-ENT-10", "author: must be 1 to 80 characters (got " + shown(e.author) + ")");
    // withdrawn versions
    if (const Value* w = doc->find("withdrawn_versions")) {
        if (!w->is_array()) c.bad("E-ENT-02", "withdrawn_versions: must be an array of versions");
        else {
            std::set<std::string> seen;
            for (size_t i = 0; i < w->array.size(); ++i) {
                const Value& v = w->array[i];
                const std::string at = "withdrawn_versions[" + std::to_string(i) + "]";
                if (!v.is_string() || !parse_semver(v.string)) c.bad("E-ENT-11", at + ": must be a semantic version like 1.2.3");
                else if (!seen.insert(v.string).second) c.bad("E-ENT-11", at + ": " + shown(v.string) + " is listed twice");
                else e.withdrawn_versions.push_back(v.string);
            }
        }
    }
    // download link and source link must name the same GitHub repository
    if (release) {
        if (auto repo = validate::parse_github_repo_url(e.source_url)) {
            if (lower_ascii(release->owner) != lower_ascii(repo->owner) || lower_ascii(release->repo) != lower_ascii(repo->repo))
                c.bad("E-ENT-16", "package_url names the repository " + release->owner + "/" + release->repo + " but source_url names " +
                      repo->owner + "/" + repo->repo + "; both must be the same repository");
        }
    }
    // text that looks like an e-mail address: a notice for the reviewer
    for (const auto& field : std::vector<std::pair<std::string, const std::string*>>{
             {"author", &e.author}, {"short_description.en", &e.short_description.en}, {"long_description.en", &e.long_description.en}})
        if (looks_like_email(*field.second))
            c.notice("W-ENT-02", field.first + " looks like it contains an e-mail address; the catalog holds no personal data beyond the author's public name");
    for (const auto& field : std::vector<std::pair<std::string, const std::optional<std::string>*>>{
             {"short_description.de", &e.short_description.de}, {"long_description.de", &e.long_description.de}})
        if (field.second->has_value() && looks_like_email(**field.second))
            c.notice("W-ENT-02", field.first + " looks like it contains an e-mail address; the catalog holds no personal data beyond the author's public name");

    if (c.errors() == 0) result.entry = std::move(e);
    return result;
}

}  // namespace pbstore
