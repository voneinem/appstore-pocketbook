#pragma once
// The install record (contracts/install-record.md): one JSON file per installed app on the reader, listing
// every file placed. Update and removal act only on what a record lists.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/manifest.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

constexpr size_t kMaxRecordBytes = 256 * 1024;

struct InstallRecord {
    std::string schema_version = "1.0";
    std::string app_id, version;
    std::string source;                     // catalog, local or adopted
    std::string package_sha256;
    std::vector<FileEntry> files;           // exactly the files placed
    std::vector<std::string> directories;   // directories the operation created
    MenuEntry menu_entry;
    std::string entry_sha256;               // of the text of the entry value as written (or found)
    std::optional<std::string> data_dir;    // system/<name> or none
    std::string written_at;                 // YYYY-MM-DDTHH:MM:SSZ
    std::string installed_by;               // "pbstore-install <version>"
};

struct RecordParse {
    std::optional<InstallRecord> record;
    std::vector<Finding> findings;   // E-INS-35: not understood (a newer format, or not valid)
};

// `expected_app_id` is the file name without ".json"; empty accepts any valid id.
RecordParse parse_install_record(const std::string& text, const std::string& expected_app_id);
// Deterministic text, ending with a newline.
std::string write_install_record(const InstallRecord& record);

// The place of a record on the reader.
std::string record_path(const std::string& app_id);

}  // namespace install
}  // namespace pbstore
