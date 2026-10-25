#include "open_url_xdg.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pbstore {
namespace install {
namespace ui {

bool XdgOpenUrl::open(const std::string& url) {
    // only an https address or the loopback address of the assistant is ever opened
    if (url.compare(0, 8, "https://") != 0 && url.compare(0, 17, "http://127.0.0.1:") != 0) return false;
    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        const int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) {
            ::dup2(null, 0);
            ::dup2(null, 1);
            ::dup2(null, 2);
        }
        const char* argv[] = {"xdg-open", url.c_str(), nullptr};
        ::execvp("xdg-open", const_cast<char* const*>(argv));
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace ui
}  // namespace install
}  // namespace pbstore
