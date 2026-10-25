#pragma once
// Where the bytes of the new files come from. The engine reads them from the verified package; tests use a
// map. What is written to the reader is checked against the hashes of the plan, not trusted.

#include <map>
#include <string>

namespace pbstore {
namespace install {

class PackageContent {
public:
    virtual ~PackageContent() = default;
    // The content of the file `path` (as listed in the plan), or false if there is none.
    virtual bool read(const std::string& path, std::string* out) = 0;
};

class MapContent : public PackageContent {
public:
    std::map<std::string, std::string> files;
    bool read(const std::string& path, std::string* out) override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        *out = it->second;
        return true;
    }
};

}  // namespace install
}  // namespace pbstore
