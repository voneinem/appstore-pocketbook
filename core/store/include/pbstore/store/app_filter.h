#pragma once
// The search of the list (spec FR-010) and its order (spec FR-009).

#include <string>
#include <vector>

#include "pbstore/store/app_row.h"

namespace pbstore {
namespace store {

// The apps whose name contains `text`, ignoring ASCII letter case; empty text returns all. The order of `rows` is kept.
std::vector<AppRow> filter_apps(const std::vector<AppRow>& rows, const std::string& text);

// Apps with an update by name (ignoring ASCII case), then the other catalog apps by name, then the apps that are not in the catalog by name.
void sort_rows(std::vector<AppRow>* rows);

}  // namespace store
}  // namespace pbstore
