#include "pbstore/install/posix_local_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace pbstore {
namespace install {

bool read_regular_file(const std::string& path, uint64_t max_bytes, std::string* out, std::string* why) {
    auto refuse = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return refuse(errno == ENOENT ? "the file does not exist" : "the file cannot be read");
    if (S_ISLNK(st.st_mode)) return refuse("it is a symbolic link; choose the file itself");
    if (S_ISDIR(st.st_mode)) return refuse("it is a folder, not a package file");
    if (!S_ISREG(st.st_mode)) return refuse("it is not a regular file (a pipe or a device cannot be used)");
    if (static_cast<uint64_t>(st.st_size) > max_bytes) return refuse("it is larger than the 64 MiB a package may have");
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return refuse("the file cannot be opened");
    struct stat fst;
    if (::fstat(fd, &fst) != 0 || !S_ISREG(fst.st_mode)) {
        ::close(fd);
        return refuse("it is not a regular file");
    }
    std::string data;
    data.reserve(static_cast<size_t>(fst.st_size));
    char buf[65536];
    while (true) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return refuse("the file cannot be read");
        }
        if (n == 0) break;
        data.append(buf, static_cast<size_t>(n));
        if (data.size() > static_cast<size_t>(max_bytes)) {
            ::close(fd);
            return refuse("it is larger than the 64 MiB a package may have");
        }
    }
    ::close(fd);
    *out = std::move(data);
    return true;
}

}  // namespace install
}  // namespace pbstore
