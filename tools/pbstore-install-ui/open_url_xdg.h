#pragma once
// Opens an address in the user's browser with xdg-open (fork and exec, no shell).

#include "pbstore/install/open_url.h"

namespace pbstore {
namespace install {
namespace ui {

class XdgOpenUrl : public OpenUrl {
public:
    bool open(const std::string& url) override;
};

}  // namespace ui
}  // namespace install
}  // namespace pbstore
