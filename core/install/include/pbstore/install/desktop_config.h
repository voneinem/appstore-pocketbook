#pragma once
// The reader's desktop configuration (system/config/desktop/view.json): shape check and the three edits the
// installer may make (add, replace or remove one app's entry). The file belongs to the device, so it is
// changed by splicing text: every byte outside the changed spans stays as it was (spec FR-014, FR-015).
// Pure: no I/O, no clock.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/json.h"
#include "pbstore/manifest.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

// An entry's "path" is the path as the device sees its storage.
constexpr const char* kDeviceRootPrefix = "/mnt/ext1/";

struct EntryValue {
    std::string path;               // /mnt/ext1/applications/x.app
    std::string title;
    std::string icon_path;          // relative, as in the manifest
    std::string focused_icon_path;
};

// path = kDeviceRootPrefix + the manifest's path; title and the two icon paths unchanged.
EntryValue entry_value_from_manifest(const MenuEntry& menu);

struct DesktopConfig {
    std::string text;
    std::string sha256;     // of `text`
    json::Value tree;       // with byte spans
    bool has_entry(const std::string& id) const;
    bool has_group(const std::string& title) const;
    std::vector<std::string> group_titles() const;
};

struct ParseResult {
    std::optional<DesktopConfig> config;
    std::vector<Finding> findings;   // E-INS-12 (not valid JSON) or E-INS-13 (expected parts missing)
};

ParseResult parse_desktop_config(const std::string& text);

struct Change {
    enum class Kind { Add, Replace, Remove };
    Kind kind = Kind::Add;
    std::string id;
    EntryValue value;
    std::string group;   // title of the group the entry belongs to (Add, Replace)
};

Change add_entry(const std::string& id, const EntryValue& value, const std::string& group);
Change replace_entry(const std::string& id, const EntryValue& value, const std::string& group);
Change remove_entry(const std::string& id);

// A replacement of old[begin, end) by `text`. begin and end are offsets in the old text.
struct Splice {
    size_t begin = 0, end = 0;
    std::string text;
};

struct EditResult {
    std::optional<std::string> text;   // the new file; unset if the edit is refused
    std::vector<Splice> splices;       // ascending, not overlapping; empty if nothing changes
    std::vector<Finding> findings;     // E-INS-14 (no such group), E-INS-15 (id already used)
};

EditResult edit(const DesktopConfig& config, const Change& change);

// The text of the entry's value exactly as it stands in the file ("" if there is no such entry). Its SHA-256
// is what the install record keeps to find out later whether the entry was changed.
std::string entry_value_text(const DesktopConfig& config, const std::string& id);

// True only if `new_text` parses, its tree equals the old tree plus exactly the intended change, and it
// equals the old text with only the changed spans replaced. `why` names the first difference.
bool verify(const std::string& old_text, const std::string& new_text, const Change& change, std::string* why = nullptr);

}  // namespace install
}  // namespace pbstore
