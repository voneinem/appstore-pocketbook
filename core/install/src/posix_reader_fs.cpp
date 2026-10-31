#include "pbstore/install/posix_reader_fs.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <strings.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

namespace pbstore {
namespace install {

namespace {

Kind kind_of(mode_t m) {
    if (S_ISREG(m)) return Kind::File;
    if (S_ISDIR(m)) return Kind::Dir;
    if (S_ISLNK(m)) return Kind::Link;
    return Kind::Other;
}

std::vector<std::string> split(const Path& p) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < p.size()) {
        const size_t slash = p.find('/', start);
        parts.push_back(p.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return parts;
}

class FdLock : public Lock {
public:
    explicit FdLock(int fd) : fd_(fd) {}
    ~FdLock() override {
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
    }

private:
    int fd_;
};

struct Fd {
    int fd;
    explicit Fd(int f) : fd(f) {}
    ~Fd() {
        if (fd >= 0) ::close(fd);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

bool write_all(int fd, const char* data, size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(fd, data, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

}  // namespace

PosixReaderFs::PosixReaderFs(std::string root) : root_(std::move(root)) {
    root_fd_ = ::open(root_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

PosixReaderFs::~PosixReaderFs() {
    if (root_fd_ >= 0) ::close(root_fd_);
}

int PosixReaderFs::open_dir(const Path& dir, bool* link) const {
    if (link) *link = false;
    if (root_fd_ < 0) return -1;
    int fd = ::dup(root_fd_);
    if (fd < 0) return -1;
    for (const std::string& part : split(dir)) {
        const int next = ::openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            struct stat st;
            if (link && ::fstatat(fd, part.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(st.st_mode)) *link = true;
            ::close(fd);
            return -1;
        }
        ::close(fd);
        fd = next;
    }
    return fd;
}

Stat PosixReaderFs::stat(const Path& p) {
    Stat s;
    if (!valid_relative_path(p) || root_fd_ < 0) return s;
    if (p.empty()) {
        struct stat st;
        if (::fstat(root_fd_, &st) == 0) s.kind = Kind::Dir;
        return s;
    }
    bool link = false;
    Fd parent(open_dir(parent_path(p), &link));
    if (parent.fd < 0) {
        if (link) s.kind = Kind::Link;   // a link on the way
        return s;
    }
    struct stat st;
    if (::fstatat(parent.fd, base_name(p).c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) return s;
    s.kind = kind_of(st.st_mode);
    if (s.kind == Kind::File) s.size = static_cast<uint64_t>(st.st_size);
    return s;
}

std::vector<DirEntry> PosixReaderFs::list(const Path& dir) {
    std::vector<DirEntry> out;
    if (!valid_relative_path(dir)) return out;
    Fd fd(open_dir(dir, nullptr));
    if (fd.fd < 0) return out;
    DIR* d = ::fdopendir(::dup(fd.fd));
    if (!d) return out;
    // The duplicate shares its read position with the handle it came from, and the handle of the root folder is
    // kept for the life of the object: without the rewind a second listing of the root would start where the
    // first one ended and miss everything.
    ::rewinddir(d);
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        struct stat st;
        if (::fstatat(fd.fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
        DirEntry de;
        de.name = name;
        de.kind = kind_of(st.st_mode);
        if (de.kind == Kind::File) de.size = static_cast<uint64_t>(st.st_size);
        out.push_back(de);
    }
    ::closedir(d);
    return out;
}

bool PosixReaderFs::read_file(const Path& p, uint64_t max_bytes, std::string* out) {
    if (p.empty() || !valid_relative_path(p)) return false;
    Fd parent(open_dir(parent_path(p), nullptr));
    if (parent.fd < 0) return false;
    Fd f(::openat(parent.fd, base_name(p).c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    if (f.fd < 0) return false;
    struct stat st;
    if (::fstat(f.fd, &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (static_cast<uint64_t>(st.st_size) > max_bytes) return false;
    std::string data;
    data.reserve(static_cast<size_t>(st.st_size));
    char buf[65536];
    while (true) {
        const ssize_t n = ::read(f.fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) break;
        data.append(buf, static_cast<size_t>(n));
        if (data.size() > max_bytes) return false;
    }
    if (out) *out = std::move(data);
    return true;
}

bool PosixReaderFs::create_exclusive(const Path& p, const std::string& data, bool flush) {
    if (p.empty() || !valid_relative_path(p)) return false;
    Fd parent(open_dir(parent_path(p), nullptr));
    if (parent.fd < 0) return false;
    const std::string name = base_name(p);
    const int fd = ::openat(parent.fd, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    bool ok = write_all(fd, data.data(), data.size());
    if (ok && flush) ok = ::fsync(fd) == 0;
    ok = (::close(fd) == 0) && ok;
    if (!ok) ::unlinkat(parent.fd, name.c_str(), 0);   // do not leave a half-written file behind
    return ok;
}

bool PosixReaderFs::rename_free(const Path& from, const Path& to) {
    if (from.empty() || to.empty() || !valid_relative_path(from) || !valid_relative_path(to)) return false;
    Fd pf(open_dir(parent_path(from), nullptr));
    Fd pt(open_dir(parent_path(to), nullptr));
    if (pf.fd < 0 || pt.fd < 0) return false;
    const std::string nf = base_name(from), nt = base_name(to);
    struct stat st;
    if (::fstatat(pf.fd, nf.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 || S_ISLNK(st.st_mode)) return false;
    // renameat2 has no wrapper in every C library (musl): call the system call itself
    if (::syscall(SYS_renameat2, pf.fd, nf.c_str(), pt.fd, nt.c_str(), static_cast<unsigned>(RENAME_NOREPLACE)) == 0) return true;
    if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP && errno != EOPNOTSUPP) return false;
    // the file system cannot refuse to replace: check first (the caller holds the lock on the reader)
    if (::fstatat(pt.fd, nt.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0) return false;
    DIR* d = ::fdopendir(::dup(pt.fd));
    if (!d) return false;
    bool collision = false;
    while (dirent* e = ::readdir(d)) {
        if (::strcasecmp(e->d_name, nt.c_str()) == 0 && !(pf.fd == pt.fd && nf == e->d_name)) collision = true;
    }
    ::closedir(d);
    if (collision) return false;
    return ::renameat(pf.fd, nf.c_str(), pt.fd, nt.c_str()) == 0;
}

bool PosixReaderFs::remove_file(const Path& p) {
    if (p.empty() || !valid_relative_path(p)) return false;
    Fd parent(open_dir(parent_path(p), nullptr));
    if (parent.fd < 0) return false;
    struct stat st;
    if (::fstatat(parent.fd, base_name(p).c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 || S_ISDIR(st.st_mode)) return false;
    return ::unlinkat(parent.fd, base_name(p).c_str(), 0) == 0;   // a link is removed itself, never followed
}

bool PosixReaderFs::make_dir(const Path& p) {
    if (p.empty() || !valid_relative_path(p)) return false;
    Fd parent(open_dir(parent_path(p), nullptr));
    if (parent.fd < 0) return false;
    return ::mkdirat(parent.fd, base_name(p).c_str(), 0755) == 0;
}

bool PosixReaderFs::remove_dir(const Path& p) {
    if (p.empty() || !valid_relative_path(p)) return false;
    Fd parent(open_dir(parent_path(p), nullptr));
    if (parent.fd < 0) return false;
    struct stat st;
    if (::fstatat(parent.fd, base_name(p).c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode)) return false;
    return ::unlinkat(parent.fd, base_name(p).c_str(), AT_REMOVEDIR) == 0;
}

bool PosixReaderFs::flush_dir(const Path& dir) {
    if (!valid_relative_path(dir)) return false;
    Fd fd(open_dir(dir, nullptr));
    if (fd.fd < 0) return false;
    return ::fsync(fd.fd) == 0;
}

bool PosixReaderFs::flush_volume() {
    if (root_fd_ < 0) return false;
    if (::syncfs(root_fd_) == 0) return true;
    ::sync();
    return true;
}

Space PosixReaderFs::space() {
    Space s;
    s.writable = false;
    if (root_fd_ < 0) return s;
    struct statvfs sv;
    if (::fstatvfs(root_fd_, &sv) != 0) return s;
    s.free_bytes = static_cast<uint64_t>(sv.f_bavail) * static_cast<uint64_t>(sv.f_bsize);
    s.cluster_bytes = static_cast<uint64_t>(sv.f_bsize);
    s.read_only = (sv.f_flag & ST_RDONLY) != 0;
    s.writable = !s.read_only && ::faccessat(root_fd_, ".", W_OK | X_OK, AT_EACCESS) == 0;
    if (s.writable) {
        // the folder the installer writes most
        Fd apps(open_dir("applications", nullptr));
        if (apps.fd >= 0 && ::faccessat(apps.fd, ".", W_OK | X_OK, AT_EACCESS) != 0) s.writable = false;
    }
    return s;
}

std::unique_ptr<Lock> PosixReaderFs::lock() {
    if (root_fd_ < 0) return nullptr;
    for (const char* dir : {"system", "system/pbstore"}) {
        if (!stat(dir).exists() && !make_dir(dir)) return nullptr;
    }
    Fd parent(open_dir("system/pbstore", nullptr));
    if (parent.fd < 0) return nullptr;
    const int fd = ::openat(parent.fd, "lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return nullptr;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return nullptr;
    }
    return std::make_unique<FdLock>(fd);
}

}  // namespace install
}  // namespace pbstore
