#include "pbstore/install/posix_state_dirs.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace pbstore {
namespace install {

namespace {

std::string default_dir() {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    if (xdg && *xdg == '/') return std::string(xdg) + "/pbstore-installer";
    const char* home = std::getenv("HOME");
    if (home && *home == '/') return std::string(home) + "/.local/state/pbstore-installer";
    return "/tmp/pbstore-installer-" + std::to_string(::getuid());
}

bool valid_name(const std::string& name) {
    return !name.empty() && name.find('/') == std::string::npos && name != "." && name != "..";
}

bool write_all(int fd, const std::string& data) {
    const char* p = data.data();
    size_t n = data.size();
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

bool make_path_private(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const std::string part = path.substr(0, i);
            if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return false;
        }
    }
    return true;
}

}  // namespace

PosixStateDirs::PosixStateDirs(std::string dir) : dir_(dir.empty() ? default_dir() : std::move(dir)) {}

bool PosixStateDirs::ensure_dir() {
    struct stat st;
    if (::stat(dir_.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    if (!make_path_private(dir_)) return false;
    ::chmod(dir_.c_str(), 0700);
    return true;
}

bool PosixStateDirs::read(const std::string& name, std::string* out) {
    if (!valid_name(name)) return false;
    const int fd = ::open((dir_ + "/" + name).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    std::string data;
    char buf[8192];
    while (true) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data.append(buf, static_cast<size_t>(n));
        if (data.size() > (16u << 20)) {
            ::close(fd);
            return false;
        }
    }
    ::close(fd);
    if (out) *out = std::move(data);
    return true;
}

bool PosixStateDirs::write_atomic(const std::string& name, const std::string& data) {
    if (!valid_name(name) || !ensure_dir()) return false;
    const std::string tmp = dir_ + "/" + name + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = write_all(fd, data) && ::fsync(fd) == 0;
    ok = (::close(fd) == 0) && ok;
    if (ok) ok = ::rename(tmp.c_str(), (dir_ + "/" + name).c_str()) == 0;
    if (!ok) ::unlink(tmp.c_str());
    return ok;
}

bool PosixStateDirs::append(const std::string& name, const std::string& data) {
    if (!valid_name(name) || !ensure_dir()) return false;
    const int fd = ::open((dir_ + "/" + name).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = write_all(fd, data);
    ok = (::close(fd) == 0) && ok;
    return ok;
}

bool PosixStateDirs::rename(const std::string& from, const std::string& to) {
    if (!valid_name(from) || !valid_name(to)) return false;
    return ::rename((dir_ + "/" + from).c_str(), (dir_ + "/" + to).c_str()) == 0;
}

bool PosixStateDirs::remove(const std::string& name) {
    if (!valid_name(name)) return false;
    return ::unlink((dir_ + "/" + name).c_str()) == 0;
}

uint64_t PosixStateDirs::size(const std::string& name) {
    struct stat st;
    if (!valid_name(name) || ::stat((dir_ + "/" + name).c_str(), &st) != 0) return UINT64_MAX;
    return static_cast<uint64_t>(st.st_size);
}

}  // namespace install
}  // namespace pbstore
