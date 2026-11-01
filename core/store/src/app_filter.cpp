#include "pbstore/store/app_filter.h"

#include <algorithm>
#include <cctype>

namespace pbstore {
namespace store {

namespace {
std::string fold(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');   // ASCII only: other characters must match exactly
    return s;
}
}  // namespace

std::vector<AppRow> filter_apps(const std::vector<AppRow>& rows, const std::string& text) {
    if (text.empty()) return rows;
    const std::string needle = fold(text);
    std::vector<AppRow> out;
    for (const AppRow& r : rows)
        if (fold(r.name).find(needle) != std::string::npos) out.push_back(r);
    return out;
}

void sort_rows(std::vector<AppRow>* rows) {
    // feature 004 (spec FR-013): the apps with an update first, then the other catalog apps, then the apps that are not in the catalog
    auto group = [](const AppRow& r) { return r.state == RowState::UpdateAvailable ? 0 : r.state == RowState::NotInCatalog ? 2 : 1; };
    std::stable_sort(rows->begin(), rows->end(), [&group](const AppRow& a, const AppRow& b) {
        const int ga = group(a), gb = group(b);
        if (ga != gb) return ga < gb;
        return fold(a.name) < fold(b.name);
    });
}

}  // namespace store
}  // namespace pbstore
