#pragma once
// The plan text of the engine with the list of files folded. Only the per-file lines ("  add|replace|remove|keep  path ...") are taken
// out, and only when there are more than kPlanFilesShown of them; the full text is the engine's, byte for byte (spec FR-014).

#include <cstddef>
#include <string>

namespace pbstore {
namespace store {

constexpr size_t kPlanFilesShown = 8;

struct PlanTextParts {
    std::string full;      // the engine's text
    std::string brief;     // without the per-file lines when there are more than kPlanFilesShown, else equal to full
    size_t file_lines = 0;
};

PlanTextParts split_plan_files(const std::string& plan_text);

}  // namespace store
}  // namespace pbstore
