#include "pbstore/install/posix_mount_source.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace pbstore {
namespace install {

namespace {

// /proc/self/mountinfo writes spaces, tabs, newlines and backslashes as \040, \011, \012, \134
std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() + 0 && s[i + 1] >= '0' && s[i + 1] <= '7' && s[i + 2] >= '0' && s[i + 2] <= '7' && s[i + 3] >= '0' && s[i + 3] <= '7') {
            out.push_back(static_cast<char>(((s[i + 1] - '0') << 6) | ((s[i + 2] - '0') << 3) | (s[i + 3] - '0')));
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::vector<std::string> subdirectories(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        struct stat st;
        if (::stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) out.push_back(dir + "/" + n);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

std::vector<Mount> PosixMountSource::mounts() {
    std::vector<Mount> out;
    std::ifstream f("/proc/self/mountinfo");
    std::string line;
    while (std::getline(f, line)) {
        // id parent major:minor root mount_point options [optional fields] - fstype source superoptions
        std::istringstream in(line);
        std::vector<std::string> head;
        std::string tok;
        while (in >> tok && tok != "-") head.push_back(tok);
        std::string fstype;
        in >> fstype;
        if (head.size() < 6 || fstype.empty()) continue;
        Mount m;
        m.mount_point = unescape(head[4]);
        m.fstype = fstype;
        if (fstype == "vfat" || fstype == "msdos" || fstype == "exfat") {
            struct statvfs sv;
            if (::statvfs(m.mount_point.c_str(), &sv) == 0) m.size_bytes = static_cast<uint64_t>(sv.f_blocks) * static_cast<uint64_t>(sv.f_frsize);
        }
        out.push_back(m);
    }
    return out;
}

std::vector<std::string> PosixMountSource::extra_roots() {
    std::vector<std::string> out;
    for (const char* base : {"/run/media", "/media"}) {
        for (const std::string& user : subdirectories(base)) {
            for (const std::string& drive : subdirectories(user)) out.push_back(drive);   // /run/media/<user>/<label>
            out.push_back(user);                                                           // /media/<label>
        }
    }
    for (const std::string& d : subdirectories("/mnt")) out.push_back(d);
    return out;
}

std::vector<std::string> PosixMountSource::usb_manufacturers() {
    std::vector<std::string> out;
    for (const std::string& dev : subdirectories("/sys/bus/usb/devices")) {
        std::ifstream f(dev + "/manufacturer");
        std::string m;
        if (std::getline(f, m) && !m.empty()) out.push_back(m);
    }
    return out;
}

std::vector<std::string> PosixMountSource::mtp_mounts() {
    std::vector<std::string> out;
    const std::string gvfs = "/run/user/" + std::to_string(::getuid()) + "/gvfs";
    for (const std::string& d : subdirectories(gvfs))
        if (d.find("/mtp:") != std::string::npos) out.push_back(d);
    return out;
}

}  // namespace install
}  // namespace pbstore
