#include "assets.h"

#include <memory>
#include <vector>

#include "assets.inc"

namespace pbstore {
namespace install {
namespace ui {

const Asset* find_asset(const std::string& name) {
    static const std::vector<Asset> assets = {
        {"index.html", "text/html; charset=utf-8", std::string(reinterpret_cast<const char*>(kAsset_index_html), kAssetSize_index_html)},
        {"app.js", "text/javascript; charset=utf-8", std::string(reinterpret_cast<const char*>(kAsset_app_js), kAssetSize_app_js)},
        {"style.css", "text/css; charset=utf-8", std::string(reinterpret_cast<const char*>(kAsset_style_css), kAssetSize_style_css)},
    };
    for (const Asset& a : assets)
        if (name == a.name) return &a;
    return nullptr;
}

}  // namespace ui
}  // namespace install
}  // namespace pbstore
