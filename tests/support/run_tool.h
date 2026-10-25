#pragma once
// Runs the pbstore-catalog program (PBSTORE_TOOL) and captures exit code, stdout and stderr.

#include <cstdlib>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pbtest {

struct RunResult {
    int code = -1;
    std::string out, err;
};

inline std::string read_fd(int fd) {
    std::string s;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0) s.append(buf, static_cast<size_t>(n));
    return s;
}

// `env` entries are "NAME=value" added to (or replacing) the environment; names in `unset` are removed.
// Runs the program at `program`.
inline RunResult run_program(const char* program, const std::vector<std::string>& args, const std::vector<std::string>& env = {},
                             const std::vector<std::string>& unset = {}, const std::string& cwd = "", const std::string& stdin_text = "") {
    int po[2], pe[2], pi[2];
    if (::pipe(po) || ::pipe(pe) || ::pipe(pi)) return {};
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(pi[0], 0);
        ::dup2(po[1], 1);
        ::dup2(pe[1], 2);
        for (int fd : {po[0], po[1], pe[0], pe[1], pi[0], pi[1]}) ::close(fd);
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) ::_exit(126);
        for (const std::string& n : unset) ::unsetenv(n.c_str());
        for (const std::string& e : env) {
            const size_t eq = e.find('=');
            ::setenv(e.substr(0, eq).c_str(), e.substr(eq + 1).c_str(), 1);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program));
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        ::execv(program, argv.data());
        ::_exit(127);
    }
    ::close(po[1]);
    ::close(pe[1]);
    ::close(pi[0]);
    if (!stdin_text.empty()) {
        ssize_t w = ::write(pi[1], stdin_text.data(), stdin_text.size());
        (void)w;
    }
    ::close(pi[1]);
    RunResult r;
    // stdout and stderr are small in these tests, so reading one after the other cannot deadlock
    r.out = read_fd(po[0]);
    r.err = read_fd(pe[0]);
    ::close(po[0]);
    ::close(pe[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}


// Runs the pbstore-catalog program (PBSTORE_TOOL).
inline RunResult run_tool(const std::vector<std::string>& args, const std::vector<std::string>& env = {},
                          const std::vector<std::string>& unset = {}, const std::string& cwd = "",
                          const std::string& stdin_text = "") {
    return run_program(PBSTORE_TOOL, args, env, unset, cwd, stdin_text);
}

}  // namespace pbtest
