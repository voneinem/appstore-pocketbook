#pragma once
// Finding the reader (spec FR-001, FR-004, FR-005, FR-009). Mounted FAT-family drives and the usual
// removable-media folders are candidates; two marker paths decide which is a reader. Several candidates are
// never reduced to one on the user's behalf.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/mount_source.h"
#include "pbstore/install/reader_fs.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

constexpr const char* kMarkerApplications = "applications/";
constexpr const char* kMarkerDesktop = "system/config/desktop/view.json";

struct Candidate {
    std::string label;          // the last component of the mount point: display only
    std::string mount_point;
    std::string fstype;
    uint64_t size_bytes = 0;
    bool qualifies = false;     // both markers exist
    std::vector<std::string> missing;
};

struct LocateResult {
    std::vector<Candidate> candidates;
    std::vector<std::string> notes;    // for example an MTP mount that cannot be used
    bool pocketbook_on_usb = false;    // a USB device from PocketBook is attached
    bool mtp_seen = false;
};

LocateResult locate(MountSource& source, const ReaderFsFactory& open);

struct Pick {
    std::optional<Candidate> reader;
    std::vector<Finding> findings;     // E-INS-01, E-INS-02 or E-INS-04 when no single reader results
};
// The reader if exactly one candidate qualifies; otherwise the finding that says why not.
Pick pick_single_reader(const LocateResult& located);

struct Opened {
    Candidate candidate;
    std::vector<Finding> findings;     // E-INS-03 when the folder is not a reader
};
// A folder chosen by hand passes the same test.
Opened open_folder(const std::string& folder, const ReaderFsFactory& open);

// The marker paths that are missing (empty for a reader).
std::vector<std::string> missing_markers(ReaderFs& fs);
// E-INS-05 (mounted read-only) or E-INS-06 (this user cannot write); empty if the reader can be written.
std::vector<Finding> check_writable(ReaderFs& fs);

}  // namespace install
}  // namespace pbstore
