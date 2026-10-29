#include "pbstore/store/plan_text.h"

namespace pbstore {
namespace store {

namespace {

bool is_file_line(const std::string& line) {
    static const char* const kOps[] = {"add", "replace", "remove", "keep"};
    if (line.compare(0, 2, "  ") != 0) return false;
    for (const char* op : kOps) {
        const std::string o = op;
        if (line.compare(2, o.size(), o) == 0 && line.size() > 2 + o.size() && line[2 + o.size()] == ' ') return true;
    }
    return false;
}

}  // namespace

PlanTextParts split_plan_files(const std::string& plan_text) {
    PlanTextParts parts;
    parts.full = plan_text;
    std::string kept;
    bool noted = false;
    size_t pos = 0;
    // first pass: count, so that the note can name the number
    while (pos < plan_text.size()) {
        const size_t end = plan_text.find('\n', pos);
        const std::string line = plan_text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (is_file_line(line)) ++parts.file_lines;
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    if (parts.file_lines <= kPlanFilesShown) {
        parts.brief = plan_text;
        return parts;
    }
    pos = 0;
    while (pos < plan_text.size()) {
        const size_t end = plan_text.find('\n', pos);
        const std::string line = plan_text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        const std::string eol = end == std::string::npos ? "" : "\n";
        if (is_file_line(line)) {
            if (!noted) {
                kept += "  " + std::to_string(parts.file_lines) + " files (not listed here)" + eol;
                noted = true;
            }
        } else {
            kept += line + eol;
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    parts.brief = kept;
    return parts;
}

}  // namespace store
}  // namespace pbstore
