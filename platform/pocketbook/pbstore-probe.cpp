// pbstore-probe: a small console program for the device task DS-01 (and the read-only parts of DS-02, DS-03, DS-04). It runs the
// file-system calls the engine relies on in a scratch folder on the reader's FAT storage and prints one line per fact:
//
//     <name>: ok|fail|n/a (<detail>)
//
// It never reads or prints a serial number, a network address, a network name, a user name or a path (spec FR-039). It changes
// nothing except its own work folder inside the scratch folder (removed again) and report.txt. The InkView calls are behind
// PBSTORE_ON_READER, so the same source builds on the computer for the host test.
//
//   pbstore-probe <scratch-folder> [--wifi] [--battery] [--identity] [--https]
//
// Without a mode it checks the file system, memory and tools (the first eight lines below); with modes it prints only those lines.
//   renameat2-noreplace  a rename onto an existing name is refused (the engine's "never replace" step)
//   syncfs               the volume can be flushed
//   flock                an exclusive lock on a file is refused to a second holder (the engine's lock)
//   o_nofollow           opening a link with O_NOFOLLOW is refused (n/a where the volume cannot make links)
//   fsync                a file and its folder can be flushed
//   free-space           statvfs: free bytes and the cluster size
//   meminfo              MemAvailable
//   tools                which of cp cmp cksum sync stdbuf df the launcher finds
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef PBSTORE_ON_READER
#include "inkview.h"
#endif

#include "pbstore/store/proc_meminfo.h"

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

namespace {

struct Line {
    std::string name, status, detail;
    std::string text() const { return name + ": " + status + (detail.empty() ? "" : " (" + detail + ")"); }
};

Line line(const char* name, const char* status, const std::string& detail = "") { return Line{name, status, detail}; }
std::string err() { return "errno " + std::to_string(errno); }

bool write_file(const std::string& path, const std::string& data) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    const bool ok = ::write(fd, data.data(), data.size()) == static_cast<ssize_t>(data.size());
    return (::close(fd) == 0) && ok;
}

// ---- the work folder, removed again ------------------------------------------------------------------------------------
struct Work {
    std::string dir;
    std::vector<std::string> files;
    explicit Work(const std::string& scratch) : dir(scratch + "/.pbstore-probe-work") { ::mkdir(dir.c_str(), 0755); }
    std::string file(const std::string& name) {
        files.push_back(name);
        return dir + "/" + name;
    }
    ~Work() {
        for (const std::string& f : files) ::unlink((dir + "/" + f).c_str());
        ::rmdir(dir.c_str());
    }
};

Line check_renameat2(Work& w) {
    const std::string a = w.file("a"), b = w.file("b"), c = w.file("c");
    if (!write_file(a, "A") || !write_file(b, "B")) return line("renameat2-noreplace", "fail", "cannot create files " + err());
    errno = 0;
    const long r = ::syscall(SYS_renameat2, AT_FDCWD, a.c_str(), AT_FDCWD, b.c_str(), static_cast<unsigned>(RENAME_NOREPLACE));
    if (r == 0) return line("renameat2-noreplace", "fail", "it replaced the existing file");
    if (errno == ENOSYS || errno == EINVAL) return line("renameat2-noreplace", "n/a", "not supported, " + err() + "; the engine checks and then renames");
    if (errno != EEXIST) return line("renameat2-noreplace", "fail", "unexpected " + err());
    errno = 0;
    if (::syscall(SYS_renameat2, AT_FDCWD, a.c_str(), AT_FDCWD, c.c_str(), static_cast<unsigned>(RENAME_NOREPLACE)) != 0)
        return line("renameat2-noreplace", "fail", "a rename into a free name failed, " + err());
    return line("renameat2-noreplace", "ok", "a rename onto an existing name was refused");
}

Line check_syncfs(const std::string& scratch) {
    const int fd = ::open(scratch.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return line("syncfs", "fail", "cannot open the folder, " + err());
    errno = 0;
    const int r = ::syncfs(fd);
    const std::string e = err();
    ::close(fd);
    return r == 0 ? line("syncfs", "ok") : line("syncfs", "fail", e);
}

Line check_flock(Work& w) {
    const std::string path = w.file("lock");
    const int a = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (a < 0) return line("flock", "fail", "cannot create the lock file, " + err());
    errno = 0;
    if (::flock(a, LOCK_EX | LOCK_NB) != 0) {
        const std::string e = err();
        ::close(a);
        return line("flock", "fail", "the first lock failed, " + e);
    }
    const int b = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    errno = 0;
    const bool second = b >= 0 && ::flock(b, LOCK_EX | LOCK_NB) == 0;
    const int second_errno = errno;
    if (b >= 0) ::close(b);
    ::close(a);
    if (second) return line("flock", "fail", "a second holder was also given the lock");
    if (second_errno != EWOULDBLOCK) return line("flock", "fail", "the second lock failed for another reason, errno " + std::to_string(second_errno));
    return line("flock", "ok", "a second holder was refused");
}

Line check_nofollow(Work& w) {
    const std::string target = w.file("target"), link = w.file("link");
    if (!write_file(target, "x")) return line("o_nofollow", "fail", "cannot create a file, " + err());
    errno = 0;
    if (::symlink("target", link.c_str()) != 0) return line("o_nofollow", "n/a", "this volume cannot make links, " + err());
    errno = 0;
    const int fd = ::open(link.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0) {
        ::close(fd);
        return line("o_nofollow", "fail", "a link was opened");
    }
    return errno == ELOOP ? line("o_nofollow", "ok", "a link was refused") : line("o_nofollow", "fail", "refused for another reason, " + err());
}

Line check_fsync(Work& w) {
    const std::string path = w.file("data");
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return line("fsync", "fail", "cannot create a file, " + err());
    const bool wrote = ::write(fd, "0123456789", 10) == 10;
    errno = 0;
    const bool file_ok = wrote && ::fsync(fd) == 0;
    const std::string e = err();
    ::close(fd);
    if (!file_ok) return line("fsync", "fail", "the file could not be flushed, " + e);
    const int dfd = ::open(w.dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    errno = 0;
    const bool dir_ok = dfd >= 0 && ::fsync(dfd) == 0;
    const std::string de = err();
    if (dfd >= 0) ::close(dfd);
    return dir_ok ? line("fsync", "ok", "a file and its folder were flushed") : line("fsync", "fail", "the folder could not be flushed, " + de);
}

Line check_free_space(const std::string& scratch) {
    struct statvfs sv;
    if (::statvfs(scratch.c_str(), &sv) != 0) return line("free-space", "fail", err());
    const unsigned long long free_bytes = static_cast<unsigned long long>(sv.f_bavail) * sv.f_frsize;
    return line("free-space", "ok", "free " + std::to_string(free_bytes) + " bytes, cluster " + std::to_string(sv.f_bsize) + " bytes");
}

Line check_meminfo() {
    const auto v = pbstore::store::ProcMeminfo().available_bytes();
    return v ? line("meminfo", "ok", "MemAvailable " + std::to_string(*v) + " bytes") : line("meminfo", "n/a", "cannot read MemAvailable");
}

bool on_path(const std::string& name) {
    const char* path = std::getenv("PATH");
    std::string p = path ? path : "/bin:/usr/bin:/sbin:/usr/sbin";
    size_t start = 0;
    while (start <= p.size()) {
        const size_t colon = p.find(':', start);
        const std::string dir = p.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!dir.empty() && ::access((dir + "/" + name).c_str(), X_OK) == 0) return true;
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return false;
}

Line check_tools() {
    std::string found, missing;
    bool cp = false, cmp = false, cksum = false;
    for (const char* t : {"cp", "cmp", "cksum", "sync", "stdbuf", "df"}) {
        const bool here = on_path(t);
        (here ? found : missing) += (found.empty() || missing.empty() ? "" : "") + std::string(here ? (found.empty() ? "" : " ") : (missing.empty() ? "" : " ")) + t;
        if (std::string(t) == "cp") cp = here;
        if (std::string(t) == "cmp") cmp = here;
        if (std::string(t) == "cksum") cksum = here;
    }
    // the launcher needs cp and one of cmp or cksum; the rest only improve it
    const bool usable = cp && (cmp || cksum);
    return line("tools", usable ? "ok" : "fail", "found: " + (found.empty() ? "none" : found) + "; missing: " + (missing.empty() ? "none" : missing));
}

// ---- the reader-only modes ---------------------------------------------------------------------------------------------
#ifdef PBSTORE_ON_READER
Line mode_wifi() {
    const int power = GetWiFiPowerStatus();
    const int flags = QueryNetwork();
    char buf[96];
    std::snprintf(buf, sizeof buf, "powered %s, flags 0x%x, connected %s", power ? "yes" : "no", static_cast<unsigned>(flags), (flags & NET_CONNECTED) ? "yes" : "no");
    return line("wifi", "ok", buf);
}
Line mode_battery() {
    const int percent = GetBatteryPower();
    return line("battery", "ok", "percent " + std::to_string(percent) + ", charging " + (IsCharging() ? "yes" : "no") + ", cable " + (IsUSBconnected() ? "yes" : "no"));
}
Line mode_identity() {
    const char* model = GetDeviceModel();
    const char* firmware = GetSoftwareVersion();   // the serial number is never read
    return line("identity", "ok", std::string("model ") + (model && *model ? model : "unknown") + ", firmware " + (firmware && *firmware ? firmware : "unknown"));
}
Line mode_https() { return line("https", "n/a", "the handshake is checked by the store itself in device task DS-07"); }
#else
Line mode_wifi() { return line("wifi", "n/a", "not on the reader"); }
Line mode_battery() { return line("battery", "n/a", "not on the reader"); }
Line mode_identity() { return line("identity", "n/a", "not on the reader"); }
Line mode_https() { return line("https", "n/a", "not on the reader"); }
#endif

int usage() {
    std::fprintf(stderr, "usage: pbstore-probe <scratch-folder> [--wifi] [--battery] [--identity] [--https]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string scratch = argv[1];
    bool want_wifi = false, want_battery = false, want_identity = false, want_https = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--wifi") want_wifi = true;
        else if (a == "--battery") want_battery = true;
        else if (a == "--identity") want_identity = true;
        else if (a == "--https") want_https = true;
        else return usage();
    }
    struct stat st;
    if (::stat(scratch.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        std::fprintf(stderr, "pbstore-probe: the scratch folder does not exist or is not a folder\n");
        return 2;
    }
    std::vector<Line> report;
#ifdef PBSTORE_ON_READER
    if (want_wifi || want_battery || want_identity || want_https) {
        std::fprintf(stderr, "pbstore-probe: opening the InkView session\n");
        OpenScreen();   // without it the calls below crashed on the reader (2026-10-31)
    }
#endif
    if (!(want_wifi || want_battery || want_identity || want_https)) {
        Work work(scratch);
        report.push_back(check_renameat2(work));
        report.push_back(check_syncfs(scratch));
        report.push_back(check_flock(work));
        report.push_back(check_nofollow(work));
        report.push_back(check_fsync(work));
        report.push_back(check_free_space(scratch));
        report.push_back(check_meminfo());
        report.push_back(check_tools());
    } else {
        if (want_wifi) { std::fprintf(stderr, "pbstore-probe: wifi\n"); report.push_back(mode_wifi()); }
        if (want_battery) { std::fprintf(stderr, "pbstore-probe: battery\n"); report.push_back(mode_battery()); }
        if (want_identity) { std::fprintf(stderr, "pbstore-probe: identity\n"); report.push_back(mode_identity()); }
        if (want_https) { std::fprintf(stderr, "pbstore-probe: https\n"); report.push_back(mode_https()); }
    }
    std::string text;
    for (const Line& l : report) text += l.text() + "\n";
    if (!write_file(scratch + "/report.txt", text)) std::fprintf(stderr, "pbstore-probe: cannot write report.txt\n");
    std::fputs(text.c_str(), stdout);
    return 0;
}
