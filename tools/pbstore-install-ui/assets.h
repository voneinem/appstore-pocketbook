#pragma once
// The pages of the assistant, compiled into the program from tools/pbstore-install-ui/assets/.

#include <string>

namespace pbstore {
namespace install {
namespace ui {

struct Asset {
    const char* name;           // "index.html", "app.js", "style.css"
    const char* content_type;
    std::string content;
};

// nullptr if there is no such page
const Asset* find_asset(const std::string& name);

}  // namespace ui
}  // namespace install
}  // namespace pbstore
