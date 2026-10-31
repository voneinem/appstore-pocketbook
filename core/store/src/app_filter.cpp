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
    std::stable_sort(rows->begin(), rows->end(), [](const AppRow& a, const AppRow& b) {
        const bool au = a.state == RowState::NotInCatalog, bu = b.state == RowState::NotInCatalog;
        if (au != bu) return !au;
        return fold(a.name) < fold(b.name);
    });
}

}  // namespace store
}  // namespace pbstore
