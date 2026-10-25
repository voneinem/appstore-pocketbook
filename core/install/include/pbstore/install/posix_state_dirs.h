#pragma once
// The Linux state folder: $XDG_STATE_HOME/pbstore-installer, default ~/.local/state/pbstore-installer.
// The folder is private to the user (mode 0700) and so are the files in it (0600).

#include "pbstore/install/state_dirs.h"

namespace pbstore {
namespace install {

class PosixStateDirs : public StateDirs {
public:
    // `dir` overrides the default location (the test build and the tests).
    explicit PosixStateDirs(std::string dir = "");
    std::string state_dir() override { return dir_; }
    bool read(const std::string& name, std::string* out) override;
    bool write_atomic(const std::string& name, const std::string& data) override;
    bool append(const std::string& name, const std::string& data) override;
    bool rename(const std::string& from, const std::string& to) override;
    bool remove(const std::string& name) override;
    uint64_t size(const std::string& name) override;

private:
    bool ensure_dir();
    std::string dir_;
};

}  // namespace install
}  // namespace pbstore
