#include "pbstore/install/locator.h"

#include <set>

namespace pbstore {
namespace install {

namespace {

std::string trim_slash(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string last_component(const std::string& p) {
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

bool fat_family(const std::string& fstype) { return fstype == "vfat" || fstype == "msdos" || fstype == "exfat"; }

Candidate examine(const std::string& point, const std::string& fstype, uint64_t size, const ReaderFsFactory& open) {
    Candidate c;
    c.mount_point = point;
    c.label = last_component(point);
    c.fstype = fstype;
    c.size_bytes = size;
    std::unique_ptr<ReaderFs> fs = open(point);
    c.missing = fs ? missing_markers(*fs) : std::vector<std::string>{kMarkerApplications, kMarkerDesktop};
    c.qualifies = c.missing.empty();
    return c;
}

std::string describe_candidates(const std::vector<Candidate>& list) {
    std::string s;
    for (const Candidate& c : list) s += (s.empty() ? "" : "; ") + c.label + " (" + c.mount_point + ")";
    return s;
}

}  // namespace

std::vector<std::string> missing_markers(ReaderFs& fs) {
    std::vector<std::string> missing;
    if (fs.stat("applications").kind != Kind::Dir) missing.push_back(kMarkerApplications);
    if (fs.stat(kMarkerDesktop).kind != Kind::File) missing.push_back(kMarkerDesktop);
    return missing;
}

LocateResult locate(MountSource& source, const ReaderFsFactory& open) {
    LocateResult r;
    std::set<std::string> seen;
    for (const Mount& m : source.mounts()) {
        if (!fat_family(m.fstype)) continue;
        const std::string point = trim_slash(m.mount_point);
        if (!seen.insert(point).second) continue;
        r.candidates.push_back(examine(point, m.fstype, m.size_bytes, open));
    }
    for (const std::string& root : source.extra_roots()) {
        const std::string point = trim_slash(root);
        if (!seen.insert(point).second) continue;
        r.candidates.push_back(examine(point, "", 0, open));
    }
    for (const std::string& mtp : source.mtp_mounts()) {
        r.mtp_seen = true;
        r.notes.push_back("A device is connected as MTP (" + mtp + "); MTP is not supported. Set USB storage mode on the reader.");
    }
    for (const std::string& m : source.usb_manufacturers())
        if (m.find("PocketBook") != std::string::npos || m.find("Pocketbook") != std::string::npos || m.find("POCKETBOOK") != std::string::npos)
            r.pocketbook_on_usb = true;
    return r;
}

Pick pick_single_reader(const LocateResult& located) {
    Pick p;
    std::vector<Candidate> good;
    for (const Candidate& c : located.candidates)
        if (c.qualifies) good.push_back(c);
    if (good.size() == 1) {
        p.reader = good[0];
        return p;
    }
    if (good.size() > 1) {
        p.findings.push_back(make_error("E-INS-02", "reader",
                                        "several drives could be the reader: " + describe_candidates(good) +
                                            ". Name the reader's folder with --reader (the assistant lets you choose)"));
        return p;
    }
    if (located.mtp_seen) {
        p.findings.push_back(make_error("E-INS-04", "reader",
                                        "a device is connected as MTP, which is not supported (" + located.notes[0] + ")"));
    } else if (located.pocketbook_on_usb) {
        p.findings.push_back(make_error("E-INS-04", "reader",
                                        "a PocketBook seems to be connected (hint from the USB device list) but no storage of it is available"));
    } else {
        std::string message = "no reader was found";
        if (!located.candidates.empty()) {
            message += "; these drives are not a reader (they lack the folders of one): ";
            std::string list;
            for (const Candidate& c : located.candidates) list += (list.empty() ? "" : "; ") + c.label + " (" + c.mount_point + ")";
            message += list;
        }
        p.findings.push_back(make_error("E-INS-01", "reader", message));
    }
    return p;
}

Opened open_folder(const std::string& folder, const ReaderFsFactory& open) {
    Opened o;
    const std::string point = trim_slash(folder);
    o.candidate = examine(point, "", 0, open);
    if (!o.candidate.qualifies) {
        std::string parts;
        for (const std::string& m : o.candidate.missing) parts += (parts.empty() ? "" : " and ") + m;
        o.findings.push_back(make_error("E-INS-03", point, point + " is not the top folder of a reader: " + parts + " missing"));
    }
    return o;
}

std::vector<Finding> check_writable(ReaderFs& fs) {
    std::vector<Finding> f;
    const Space s = fs.space();
    if (s.read_only)
        f.push_back(make_error("E-INS-05", "reader", "the storage of the reader is mounted read-only"));
    else if (!s.writable)
        f.push_back(make_error("E-INS-06", "reader", "this user cannot write to the reader's storage"));
    return f;
}

}  // namespace install
}  // namespace pbstore
