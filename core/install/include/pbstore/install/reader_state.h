#pragma once
// One look at a reader: the install records, the desktop configuration, the state of the paths a plan will
// touch and the data folders it concerns. This is the only place that scans a reader; the planner works on
// the snapshot it returns and never touches the reader.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/desktop_config.h"
#include "pbstore/install/fingerprint.h"
#include "pbstore/install/identity.h"
#include "pbstore/install/install_record.h"
#include "pbstore/install/reader_fs.h"
#include "pbstore/manifest.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

struct InstalledApp {
    InstallRecord record;
    std::vector<std::string> changed_files;   // recorded files that are missing or whose hash differs (only with all_recorded)
};

struct DataFolderFacts {
    bool exists = false;
    uint64_t files = 0;   // counted, never read
    uint64_t bytes = 0;
};

struct ReaderSnapshot {
    Identity identity;
    Space space;
    std::string view_text;                      // "" if it cannot be read
    std::string view_sha256;                    // "" if it cannot be read
    std::optional<DesktopConfig> desktop;       // unset if view.json is missing or not acceptable
    std::vector<Finding> findings;              // E-INS-12/13 for view.json, E-INS-35 for a record that is not understood
    std::map<std::string, InstalledApp> installed;   // by app id
    std::vector<std::string> backups;           // names of the files in system/pbstore/backups
    bool journal_present = false;               // an interrupted operation was left
    std::map<std::string, TargetFact> facts;    // the state of every target path (and its parent folders)
    std::map<std::string, std::vector<DirEntry>> listings;   // the entries of every existing folder that holds a target path
    std::map<std::string, DataFolderFacts> data;             // data folders named by the manifest or a record
};

struct ScanRequest {
    std::string app_id;                  // the app the operation is about ("" for none)
    const Manifest* manifest = nullptr;  // the package that will be installed, if any
    bool all_recorded = false;           // hash every recorded file of every app (for the list of what is installed)
    std::string data_dir;                // a data folder to look at as well (for deleting it after a removal)
};

// Counts the files and the bytes of a folder from the directory entries; no file is opened. Links are not followed.
DataFolderFacts count_data_folder(ReaderFs& fs, const std::string& dir);

// The paths whose state a plan for this request depends on (and their parent folders), sorted.
std::vector<std::string> scan_target_paths(const ScanRequest& req, const std::map<std::string, InstalledApp>& installed);

ReaderSnapshot scan(ReaderFs& fs, const Identity& identity, const ScanRequest& req);

// The basis of the fingerprint for a plan, from a snapshot.
FingerprintBasis snapshot_basis(const ReaderSnapshot& snap, const std::vector<std::string>& plan_paths);

}  // namespace install
}  // namespace pbstore
