#pragma once
// The per-user state folder on the computer (serial memory and log). Nothing in it identifies the user.

#include <cstdint>
#include <string>

namespace pbstore {
namespace install {

class StateDirs {
public:
    virtual ~StateDirs() = default;
    virtual std::string state_dir() = 0;   // for display and tests; names below are relative to it
    virtual bool read(const std::string& name, std::string* out) = 0;
    // Replaces the file through a temporary file and a rename; the file is private to the user.
    virtual bool write_atomic(const std::string& name, const std::string& data) = 0;
    virtual bool append(const std::string& name, const std::string& data) = 0;
    virtual bool rename(const std::string& from, const std::string& to) = 0;   // replaces `to`
    virtual bool remove(const std::string& name) = 0;
    // Size in bytes, or UINT64_MAX if the file does not exist.
    virtual uint64_t size(const std::string& name) = 0;
};

}  // namespace install
}  // namespace pbstore
