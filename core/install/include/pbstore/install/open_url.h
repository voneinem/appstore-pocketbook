#pragma once
// Opens an address in the user's browser (the assistant). One implementation per platform; tests use a fake.

#include <string>

namespace pbstore {
namespace install {

class OpenUrl {
public:
    virtual ~OpenUrl() = default;
    // Only an https:// address or a loopback http:// address is ever passed. False if it could not be opened.
    virtual bool open(const std::string& url) = 0;
};

}  // namespace install
}  // namespace pbstore
