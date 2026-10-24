#pragma once
// Format checks shared by the catalog, the manifest and the entry file. Each function is a plain
// yes/no check of the rules in docs/contracts/.

#include <optional>
#include <string>

namespace pbstore {
namespace validate {

bool is_valid_utf8(const std::string& s);
// Number of Unicode code points (the text must be valid UTF-8).
size_t utf8_length(const std::string& s);
// Valid UTF-8 and between min_len and max_len code points.
bool text_length_between(const std::string& s, size_t min_len, size_t max_len);

bool is_app_id(const std::string& s);          // ^[a-z][a-z0-9-]{1,39}$
bool is_menu_entry_id(const std::string& s);   // ^U_[A-Za-z0-9_]{1,40}$
bool is_data_dir(const std::string& s);        // ^system/[a-z0-9][a-z0-9_-]{1,39}$
bool is_https_url(const std::string& s);       // https:// with a host, no spaces or control characters
bool is_iso_date(const std::string& s);        // YYYY-MM-DD, a real calendar date
bool is_iso_timestamp(const std::string& s);   // YYYY-MM-DDTHH:MM:SSZ
// A relative path with "/" separators: characters A-Za-z0-9._ - and "/", at most 200 characters,
// no empty, "." or ".." segment, no segment starting with ".", no leading or trailing "/".
bool is_safe_path(const std::string& s);

struct GithubRelease {
    std::string owner, repo, tag, asset;
};
// https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.zip, no query, no fragment.
std::optional<GithubRelease> parse_github_release_url(const std::string& url);

struct GithubRepo {
    std::string owner, repo;
};
// https://github.com/<owner>/<repo> (optionally with a trailing "/" or ".git"); nullopt for anything else.
std::optional<GithubRepo> parse_github_repo_url(const std::string& url);

}  // namespace validate
}  // namespace pbstore
