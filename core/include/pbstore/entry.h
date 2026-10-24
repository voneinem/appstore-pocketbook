#pragma once
// The entry file a developer adds in a pull request (docs/contracts/entry-file.md).

#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/rules.h"

namespace pbstore {

constexpr size_t kMaxEntryBytes = 16 * 1024;

struct Entry {
    std::string schema_version;
    std::string id;
    std::string package_url;
    std::string release_date;
    LocalizedText short_description, long_description;
    std::string license, source_url, author, icon_url, changelog_url;
    std::vector<std::string> withdrawn_versions;
};

struct EntryParseResult {
    std::optional<Entry> entry;          // set only if there is no error finding
    std::vector<Finding> findings;       // E-ENT-01 … E-ENT-11, E-ENT-16 and the notice W-ENT-02
};

// `file_name` is the base name of the file ("sample-app.json"); `today` is the current UTC date
// (YYYY-MM-DD) against which release_date is checked.
EntryParseResult parse_entry(const std::string& bytes, const std::string& file_name, const std::string& today);

// The license identifiers of catalog/accepted-licenses.txt (compiled in).
bool is_accepted_license(const std::string& spdx_id);
// The calendar day after an ISO date; "" if `iso_date` is not a valid date.
std::string next_day(const std::string& iso_date);
// True for text that looks like an e-mail address (local@domain.tld).
bool looks_like_email(const std::string& text);

}  // namespace pbstore
