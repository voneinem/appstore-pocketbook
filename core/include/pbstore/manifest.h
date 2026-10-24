#pragma once
// The package manifest (docs/contracts/package-manifest.md): everything a generic installer needs
// to install, update and remove an app without app-specific code.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/rules.h"

namespace pbstore {

constexpr size_t kMaxManifestBytes = 256 * 1024;

struct FileEntry {
    std::string path;     // applications/...
    int64_t size = 0;
    std::string sha256;   // 64 lowercase hex
};

struct MenuEntry {
    std::string id;            // U_...
    std::string title;
    std::string path;          // application path, relative to the storage root, listed in files
    std::string icon, focused_icon;
    std::string group;         // @General
};

struct Manifest {
    std::string schema_version;
    std::string id, name, version, min_firmware, target_device;
    std::vector<FileEntry> files;          // sorted as written by the packer
    MenuEntry menu_entry;
    std::optional<std::string> data_dir;   // system/<name>, kept on removal by default; none if the app has no data folder
};

struct ManifestParseResult {
    std::optional<Manifest> manifest;      // set only if the manifest is valid
    std::vector<Finding> findings;         // E-PKG-05 … E-PKG-09, E-PKG-13 … E-PKG-15
};

// Strict parse and validation. Unknown fields are ignored. A newer major schema is reported as
// E-PKG-06 ("needs updating"); the other fields are then not judged.
ManifestParseResult parse_manifest(const std::string& bytes);

}  // namespace pbstore
